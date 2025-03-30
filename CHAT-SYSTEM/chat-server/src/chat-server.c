/*
 * File: chat_server.c
 * Date: 2025-03-29
 * Sp_04
 * Group member: Deyi, Zhizheng
 * Description: Multi-threaded TCP chat server supporting concurrent client connections
 * Features: Client registration, message broadcasting, connection management
 * Protocols: IPv4, TCP socket communication
 * Threading: Uses pthreads for concurrent client handling
 * Limitations: Supports up to 10 concurrent clients (fixed array size)
 */

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

/* Client connection information structure */
typedef struct {
    char ip[INET_ADDRSTRLEN];   // Client IP address
    char userID[6];             // Client username (max 5 chars + null)
    int socket_fd;              // Client socket descriptor
} ClientInfo;

/* Global client management variables */
ClientInfo client_list[10];     // Fixed-size client storage
int client_count = 0;           // Current number of connected clients
volatile int shutdown_requested = 0; // Server shutdown flag (volatile for cross-thread visibility)
pthread_mutex_t client_list_mutex = PTHREAD_MUTEX_INITIALIZER; // Thread synchronization

/**
 * Adds new client to connection list
 * @param new_client ClientInfo structure containing connection details
 * Thread-safe operation using mutex locking
 */
void add_client(ClientInfo new_client) {
    pthread_mutex_lock(&client_list_mutex);
    if (client_count < 10) {
        client_list[client_count++] = new_client;
    } else {
        fprintf(stderr, "Warning: Client list is full, cannot add more clients.\n");
    }
    pthread_mutex_unlock(&client_list_mutex);
}

/**
 * Broadcasts message to all connected clients except sender
 * @param message Message content to broadcast
 * @param sender_socket Socket descriptor of sending client
 * Formats message with sender info and distributes to all recipients
 */
void broadcast_message(char *message, int sender_socket) {
    pthread_mutex_lock(&client_list_mutex);
    char sender_info[FORMAT_SIZE] = {0};

    // Build sender identification string
    for (int i = 0; i < client_count; i++) {
        if (client_list[i].socket_fd == sender_socket) {
            snprintf(sender_info, FORMAT_SIZE,
                     "%-15s [%-5s] << %-40s",
                     client_list[i].ip,
                     client_list[i].userID,
                     message);
            break;
        }
    }

    // Distribute message to all clients
    for (int i = 0; i < client_count; i++) {
        if (client_list[i].socket_fd != sender_socket) {
            send(client_list[i].socket_fd, sender_info, strlen(sender_info) + 1, 0);
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
}

/**
 * Removes client from connection list
 * @param socket_fd Socket descriptor of client to remove
 * Handles client list compaction and server shutdown condition
 */
void remove_client(int socket_fd) {
    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < client_count; i++) {
        if (client_list[i].socket_fd == socket_fd) {
            printf("User leave: %s (IP: %s)\n", client_list[i].userID, client_list[i].ip);
            
            // Compact client list array
            memmove(&client_list[i], &client_list[i + 1], 
                   (client_count - i - 1) * sizeof(ClientInfo));
            client_count--;
            
            if (client_count == 0) {
                printf("All clients disconnected. Server shutdown initiated.\n");
                close(socket_fd);
                shutdown_requested = 1;
            }
            break;
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
}

/**
 * Client connection handler thread
 * @param arg Pointer to client socket descriptor
 * Manages client registration, message handling, and cleanup
 */
void *handle_client(void *arg) {
    int new_socket = *(int *)arg;
    free(arg);  // Free allocated socket pointer

    char buffer[BUFFER_SIZE] = {0};
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    // Get client connection information
    getpeername(new_socket, (struct sockaddr *)&address, &addrlen);
    char *client_ip = inet_ntoa(address.sin_addr);

    // Initialize client structure
    ClientInfo new_client;
    strncpy(new_client.ip, client_ip, INET_ADDRSTRLEN);
    new_client.socket_fd = new_socket;

    // Process client registration
    int valread = read(new_socket, buffer, BUFFER_SIZE);
    if (valread > 0 && strncmp(buffer, "USER:", 5) == 0) {
        strncpy(new_client.userID, buffer + 5, 5);
        new_client.userID[5] = '\0';
        printf("User registered: %s (IP: %s)\n", new_client.userID, new_client.ip);
    }
    add_client(new_client);

    // Message handling loop
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        valread = read(new_socket, buffer, BUFFER_SIZE);
        
        if (valread <= 0 || strcmp(buffer, "bye") == 0) break;
        
        printf("Message from %s: %s\n", new_client.userID, buffer);
        broadcast_message(buffer, new_socket);
    }

    // Connection cleanup
    remove_client(new_socket);
    close(new_socket);
    return NULL;
}

/**
 * Main server function
 * Sets up TCP server socket and manages client connections
 * Implements non-blocking accept with 100ms sleep interval
 */
int main(void) {
    int server_fd, new_socket;
    struct sockaddr_in server_address, client_address;
    int opt = 1;
    int addrlen = sizeof(server_address);

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    // Bind and listen
    if (bind(server_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    printf("Server (PID: %d) listening on port %d...\n", getpid(), PORT);

    // Main server loop
    while (!shutdown_requested) {
        new_socket = accept(server_fd, (struct sockaddr *)&client_address, 
                           (socklen_t *)&addrlen);
        
        if (new_socket >= 0) {
            int *client_sock = malloc(sizeof(int));
            *client_sock = new_socket;
            
            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, handle_client, client_sock) != 0) {
                perror("Thread creation failed");
                free(client_sock);
                close(new_socket);
            } else {
                pthread_detach(thread_id);
            }
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("Accept error");
        }
        
        usleep(100000);  // Non-blocking sleep
    }

    // Cleanup resources
    pthread_mutex_destroy(&client_list_mutex);
    close(server_fd);
    return 0;
}