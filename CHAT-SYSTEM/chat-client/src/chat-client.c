#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 69
#define MESSAGE_SIZE 81
#define DEFAULT_SERVER "127.0.0.1" // Default server if none provided
// Flag to signal thread to exit, avoid main thread exit when child thread is still receiving message.
volatile int client_running = 1;

void *receive_messages(void *socket_ptr)
{
    int sock = *((int *)socket_ptr);
    char buffer[BUFFER_SIZE]= {0};  // Initialize buffer
    time_t rawtime;
    struct tm timeinfo;
    char timestamp[20];

    // Set socket to non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    while (client_running)
    {
        // Clear buffer
        memset(buffer, 0, BUFFER_SIZE);

        // Receive message from server
        int valread = read(sock, buffer, BUFFER_SIZE);
        if (valread > 0)
        {
            // Get current time
            time(&rawtime);
            localtime_r(&rawtime, &timeinfo);
            strftime(timestamp, sizeof(timestamp), "(%H:%M:%S)", &timeinfo);

            // Print received message
            printf("\r%s %s\n>> ", buffer, timestamp);  // Print only received bytes
            fflush(stdout); // Ensure output is displayed immediately
        }
        if (valread < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // No data available, sleep a bit and check client_running again
                usleep(100000); // 100ms
                continue;
            }
            // Actual error
            printf("\nError reading from server\n");
            client_running = 0;
            break;
        }
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    int sock = 0;
    struct sockaddr_in serv_addr;
    char userID[6] = "guest";
    char server_name[100] = DEFAULT_SERVER; // Default server
    int i;

    // Parse command line arguments
    for (i = 1; i < argc; i++)
    {
        if (strncmp(argv[i], "--user", 6) == 0)
        {
            strncpy(userID, argv[i] + 6, 5); // Copy up to 5 chars (leaving room for null)
            userID[5] = '\0';                // Ensure null termination
            printf("UserID set to: %s\n", userID);
        }
        else if (strncmp(argv[i], "--server", 8) == 0)
        {
            strncpy(server_name, argv[i] + 8, 99);
            server_name[99] = '\0'; // Ensure null termination
            printf("Server set to: %s\n", server_name);
        }
        else
        {
            printf("Usage: %s --user<userID> --server<server>\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        return EXIT_FAILURE;
    }

    // Set up server address structure
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert server name to IP address
    if (inet_pton(AF_INET, server_name, &serv_addr.sin_addr) <= 0)
    {
        // Not a valid IP address, try as hostname
        struct hostent *he;
        if ((he = gethostbyname(server_name)) == NULL)
        {
            printf("Could not resolve hostname: %s\n", server_name);
            return EXIT_FAILURE;
        }
        // Copy the first IP address from the list
        memcpy(&serv_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection Failed");
        return EXIT_FAILURE;
    }

    printf("Connected to server at %s:%d\n", server_name, PORT);

    // First send userID to register with server
    char reg_message[20] = {0};
    sprintf(reg_message, "USER:%s", userID);
    send(sock, reg_message, strlen(reg_message), 0);
    printf("Registered with server as %s\n", userID);
    printf("Enter messages (or 'bye' to quit):\n");
    // Start receive thread
    pthread_t receive_thread;
    if (pthread_create(&receive_thread, NULL, receive_messages, &sock) != 0)
    {
        perror("Failed to create receive thread");
        close(sock);
        return EXIT_FAILURE;
    }
    // Chat loop
    char message[MESSAGE_SIZE] = {0};

    while (1)
    {
        // Clear previous message
        memset(message, 0, MESSAGE_SIZE);

        // Get user input
        printf(">> ");
        fgets(message, MESSAGE_SIZE - 1, stdin);

        // Remove newline character
        message[strcspn(message, "\n")] = 0;

        // Check for exit
        if (strcmp(message, "bye") == 0)
        {
            break;
        }

        //??? prevent user input more than 80
        send(sock, message, strlen(message), 0);

    }

    client_running = 0;                 // Signal receive thread to exit
    pthread_join(receive_thread, NULL); // Wait for receive thread to finish

    // Close the socket
    close(sock);

    return 0;
}
