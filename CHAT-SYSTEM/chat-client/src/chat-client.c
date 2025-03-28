
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
#include <ncurses.h>
#include <time.h>
#include <locale.h>
#include <ctype.h>

#define PORT 8080
#define BUFFER_SIZE 69
#define MESSAGE_SIZE 81
#define DEFAULT_SERVER "127.0.0.1" // Default server if none provided
#define DISPLAY_MESSAGE_SIZE 89

// Global variable declarations
volatile int client_running = 1;
WINDOW *chat_win, *msg_win;
int shouldBlank = 0;
int row = 0;

// Function prototypes
void destroy_win(WINDOW *win);
void input_win(WINDOW *win, char *message);
void display_win(WINDOW *win, char *word, int whichRow, int shouldBlank);
void blankWin(WINDOW *win);
void *receive_messages(void *socket_ptr);

int main(int argc, char *argv[])
{
    int sock = 0;
    struct sockaddr_in serv_addr;
    char userID[6] = "guest";
    char server_name[100] = DEFAULT_SERVER; // Default server
    int i;
    // Set up server address structure
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    int chat_startx, chat_starty, chat_width, chat_height;
    int msg_startx, msg_starty, msg_width, msg_height;

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

    // Initialize ncurses
    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    scrollok(stdscr, TRUE);
    refresh();

    // Calculate window dimensions
    chat_height = 5;
    chat_width = COLS - 2;
    chat_startx = 1;
    chat_starty = LINES - chat_height - 1;

    msg_height = LINES - chat_height - 3;
    msg_width = COLS - 2;
    msg_startx = 1;
    msg_starty = 1;

    // Create message window
    msg_win = newwin(msg_height, msg_width, msg_starty, msg_startx);
    if (msg_win == NULL)
    {
        endwin();
        perror("Failed to create message window");
        return EXIT_FAILURE;
    }
    box(msg_win, 0, 0);
    scrollok(msg_win, TRUE);
    wrefresh(msg_win);

    // Create chat window
    chat_win = newwin(chat_height, chat_width, chat_starty, chat_startx);
    if (chat_win == NULL)
    {
        delwin(msg_win);
        endwin();
        perror("Failed to create chat window");
        return EXIT_FAILURE;
    }
    box(chat_win, 0, 0);
    wrefresh(chat_win);

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
        wclear(chat_win);
        box(chat_win, 0, 0);

        input_win(chat_win, message);

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

    // Cleanup ncurses windows and end curses mode
    delwin(msg_win);
    delwin(chat_win);
    endwin();

    // Close the socket
    close(sock);

    return 0;
}

void *receive_messages(void *socket_ptr)
{
    int sock = *((int *)socket_ptr);
    char buffer[BUFFER_SIZE] = {0}; // Initialize buffer
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
            // printf("\r%s %s\n>> ", buffer, timestamp);  // Print only received bytes
            // fflush(stdout); // Ensure output is displayed immediately
            char message[DISPLAY_MESSAGE_SIZE];
            snprintf(message, DISPLAY_MESSAGE_SIZE, "%s %s", buffer, timestamp);

            // Display message and increment row
            display_win(msg_win, message, row, shouldBlank);
            row++;

            // Reset row if exceeding window height
            int max_row;
            // getmaxyx(msg_win, max_row, NULL);
            if (row >= max_row - 1)
            {                    // -1 to account for borders
                row = 1;         // Reset to top row
                shouldBlank = 1; // Optional: Clear window when resetting
            }
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
    destroy_win(msg_win);
    return NULL;
}

/* This function is for taking input chars from the user */
void input_win(WINDOW *win, char *word)
{
    int i = 0, ch;
    int maxrow, maxcol, row = 1, col = 4;

    getmaxyx(win, maxrow, maxcol); /* get window size */
    memset(word, 0, MESSAGE_SIZE); // Clear previous input
    // Preserve prompt during input
    mvwprintw(win, 1, 1, ">> "); // Explicitly draw prompt
    wmove(win, row, col);        // Start input after prompt
    wrefresh(win);
    // Read input character-by-character
    while ((ch = wgetch(win)) != '\n' && ch != KEY_ENTER )
    {
        if ((ch == KEY_BACKSPACE || ch == 127) && i > 0)
        {
            // Handle backspace
            i--;
            col--;

            // Overwrite character with space (instead of deleting)
            mvwaddch(win, row, col, ' ');
            wmove(win, row, col); // Move cursor back
        }
        else if (isprint(ch) && col < maxcol - 1)
        {
            if (i < MESSAGE_SIZE - 1 ) {
                word[i++] = ch;
                waddch(win, ch);
                col++;
            } else {
                flash();  // Visual feedback instead of beep()
            }
        }
        wrefresh(win);
    }
    word[i] = '\0'; // Null-terminate
} /* input_win */

void display_win(WINDOW *win, char *word, int whichRow, int shouldBlank)
{
    if (shouldBlank == 1)
        blankWin(win);             /* make it a clean window */
    wmove(win, (whichRow + 1), 1); /* position cusor at approp row */
    wprintw(win, "%s", word);    // Add newline to trigger scrolling
    box(win, 0, 0);   // Redraw borders
    wrefresh(win);
} /* display_win */

void destroy_win(WINDOW *win)
{
    delwin(win);
} /* destory_win */

void blankWin(WINDOW *win)
{
    int i;
    int maxrow, maxcol;

    getmaxyx(win, maxrow, maxcol);
    for (i = 1; i < maxcol - 2; i++)
    {
        wmove(win, i, 1);
        refresh();
        wclrtoeol(win);
        wrefresh(win);
    }
    box(win, 0, 0); /* draw the box again */
    wrefresh(win);
}