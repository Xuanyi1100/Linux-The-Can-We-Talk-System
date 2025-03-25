#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 40
#define FORMAT_SIZE 68

typedef struct
{
    char ip[INET_ADDRSTRLEN];
    char userID[6];
    int socket_fd;
} ClientInfo;

ClientInfo client_list[10];
int client_count = 0;
// without volatile, main thread wiil never see its change
volatile int shutdown_requested = 0;
pthread_mutex_t client_list_mutex = PTHREAD_MUTEX_INITIALIZER;

void add_client(ClientInfo new_client)
{
    pthread_mutex_lock(&client_list_mutex);
    if (client_count < 10) // Check if there is space in the array
    {
        client_list[client_count++] = new_client;
    }
    else
    {
        fprintf(stderr, "Warning: Client list is full, cannot add more clients.\n");
    }
    pthread_mutex_unlock(&client_list_mutex);
}

void broadcast_message(char *message, int sender_socket)
{
    pthread_mutex_lock(&client_list_mutex);
    // Find sender info
    char sender_info[FORMAT_SIZE] = {0};  // Initialize to zero;

    for (int i = 0; i < client_count; i++)
    {
        if (client_list[i].socket_fd == sender_socket)
        {
            //snprintf() always adds a null terminator (\0) at the end, that's why + 1
            snprintf(sender_info, FORMAT_SIZE + 1, 
                    "%-15s [%-5s] << %-40s ", 
                    client_list[i].ip,
                    client_list[i].userID,                      
                    message);
            break;
        }
    }
    
    // Send formatted message to all clients
    for (int i = 0; i < client_count; i++)
    {
        if (client_list[i].socket_fd != sender_socket)
        {
            // +1 make sure \0 is sent, so the printf in client could find where to end
            send(client_list[i].socket_fd, sender_info, strlen(sender_info)+1, 0);
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
}

void remove_client(int socket_fd)
{
    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < client_count; i++)
    {
        if (client_list[i].socket_fd == socket_fd)
        {
            printf("User leave: %s (IP: %s)\n", client_list[i].userID, client_list[i].ip);
            // move behind clients forward
            memmove(&client_list[i], &client_list[i + 1], (client_count - i - 1) * sizeof(ClientInfo));
            client_count--;
            if (client_count == 0)
            {
                printf("the number of threads reaches 0, server shutdown\n");
                // Close the connected socket
                close(socket_fd);
                shutdown_requested = 1;
            }
            break;
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
}

void *handle_client(void *arg)
{
    int new_socket = *(int *)arg;
    free(arg); // Free the socket pointer we allocated

    char buffer[BUFFER_SIZE] = {0};
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    // Get client IP
    getpeername(new_socket, (struct sockaddr *)&address, &addrlen);
    char *client_ip = inet_ntoa(address.sin_addr);

    // Create and add client
    ClientInfo new_client;
    strncpy(new_client.ip, client_ip, INET_ADDRSTRLEN);
    new_client.socket_fd = new_socket;

    int valread = read(new_socket, buffer, BUFFER_SIZE);
    if (valread < 0)
    {
        perror("Error reading from client");
    }
    else if (valread == 0)
    {
        // Connection closed by client
        printf("Client disconnected\n");
    }
    // Check if message is registration message (starts with "USER:")
    if (strncmp(buffer, "USER:", 5) == 0)
    {
        // Extract username (skip the "USER:" prefix)
        char *username = buffer + 5;
        strncpy(new_client.userID, username, 5); // Copy max 5 chars
        new_client.userID[5] = '\0';             // Ensure null termination
        printf("User registered: %s (IP: %s)\n", new_client.userID, new_client.ip);
        memset(buffer, 0, BUFFER_SIZE);
    }
    add_client(new_client);

    // Client handling loop
    while (1)
    {
        int valread = read(new_socket, buffer, BUFFER_SIZE);
        if (valread <= 0)
            break; // Connection closed

        printf("Message from %s: %s\n", new_client.userID, buffer);

        if (strcmp(buffer, "bye") == 0)
        {
            break;
        }
        broadcast_message(buffer, new_socket);
        memset(buffer, 0, BUFFER_SIZE); // clear buffer
    }

    // Cleanup
    remove_client(new_socket);
    close(new_socket);
    return NULL;
}

int main(void)
{
    int server_fd, new_socket;
    struct sockaddr_in server_address, client_address;
    int opt = 1;
    int addrlen = sizeof(server_address);

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options to reuse address and port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Make socket non-blocking
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    // Set up server address structure
    server_address.sin_family = AF_INET;         // IPv4
    server_address.sin_addr.s_addr = INADDR_ANY; // Accept connections from any IP
    server_address.sin_port = htons(PORT);       // Port in network byte order

    // Bind the socket to the specified IP and port
    if (bind(server_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections (queue up to 10 connections)
    if (listen(server_fd, 10) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("Server (PID: %d) listening on port %d...\n", getpid(), PORT);

    while (!shutdown_requested)
    {
        // Accept a new connection
        new_socket = accept(server_fd, (struct sockaddr *)&client_address, (socklen_t *)&addrlen);
        if (new_socket < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // No connection available, sleep a bit and check shutdown_requested again
                usleep(100000); // 100ms
                continue;
            }
            perror("accept");
            continue;
        }

        int *client_sock = malloc(sizeof(int));
        *client_sock = new_socket;
        pthread_t thread_id;

        if (pthread_create(&thread_id, NULL, handle_client, client_sock) != 0)
        {
            perror("pthread_create");
            free(client_sock);
            close(new_socket);
        }
        else
        {
            pthread_detach(thread_id); // Don't need to join
        }
    }

    // clean mutex
    pthread_mutex_destroy(&client_list_mutex);
    // Close the listening socket
    close(server_fd);
    return 0;
}
