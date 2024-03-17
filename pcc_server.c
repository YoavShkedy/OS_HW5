#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

const int BUFF_SIZE = 1000 * 1000; // Approximately 1MB
int PROCESSING_CLIENT = 0; // Flag to indicate whether a client connection is currently being processed
int TERMINATE = 0; // Flag to indicate whether a SIGINT has been received, signaling the server to terminate
unsigned int total_pcc[127]; // Array to keep track of the total count of printable characters received from all clients

/**
 * This function iterates through the array of total printable character counts (total_pcc),
 * which tracks how many times each printable ASCII character (range 32 to 126) has been received from clients.
 * For each character in the array, it prints the character itself and its count to the standard output.
 * After printing all counts, the function terminates the server program with an exit status of 0.
 */
 void shutdown_server() {
    int i;
    for (i = 32; i < 127; i++)
        printf("char '%c' : %hu times\n", i, total_pcc[i]);
    exit(0);
}

/**
 * Handles the SIGINT signal (typically triggered by pressing Ctrl+C).
 *
 * @param sig The signal number received.
 */
void server_handler(int sig) {
    if (PROCESSING_CLIENT == 0)
        // No client is currently interacting with server, allowing for immediate termination
        shutdown_server();
    else
        // A client interaction is in progress; mark the server for termination once interaction completes
        TERMINATE = 1;
}

/**
 * Registers the SIGINT signal handler.
 */
void register_handler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &server_handler; // Set the SIGINT handler function
    sigemptyset(&sa.sa_mask); // No signals are blocked during execution of the handler
    sa.sa_flags = SA_RESTART; // Restart system calls if interrupted by handler

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Error registering SIGINT handler");
        exit(1);
    }
}

/**
 * Finalizes the handling of a client connection in error scenarios,
 * and prepares the server to accept a new client.
 *
 * @param client_socket_fd The file descriptor for the client's connection socket.
 */
void handle_client(int client_socket_fd) {
    // Close the socket for the current client
    close(client_socket_fd);

    // Reset the flag indicating that a client is currently being served
    PROCESSING_CLIENT = 0;

    // If a shutdown signal was received during client service, initiate server shutdown
    if (TERMINATE)
        shutdown_server();
}

int main(int argc, char *argv[]) {
    int sock_fd;
    int i;
    int total_printable_chars_host; // Total count of printable characters received, in host byte order
    int total_printable_chars_network; // Total count of printable characters to send, in network byte order
    int enable = 1; // Variable to enable socket options, specifically SO_REUSEADDR
    int cont_to_next_clnt = 0; // Flag to control the flow to the next client interaction

    char buffer[BUFF_SIZE]; // Buffer for temporarily storing data read from the socket

    unsigned int file_size_host; // Size of the incoming file, converted to host byte order
    unsigned int file_size_network; // Size of the incoming file from the client, in network byte order

    ssize_t bytes_read;
    ssize_t total_bytes_read;
    ssize_t bytes_sent; 
    ssize_t total_bytes_sent;


    socklen_t addr_size = sizeof(struct sockaddr_in); // Size of the sockaddr_in structure, used in socket operations

    struct sockaddr_in server_address; // Server address structure for binding the listening socket
    struct sockaddr_in client_address; // Client address structure used when accepting connections

    unsigned int client_pcc[127]; // Count of printable characters for the current client connection

    // Validate the number of command-line arguments
    if (argc != 2) {
        perror("Error. Exactly 2 arguments are required: <Executable> <Server Port>.");
        exit(1);
    }

    // Register SIGINT handler
    register_handler();

    // Initialize a TCP socket for IPv4 communication
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Failed to create a TCP socket.");
        exit(1);
    }

    // Configure the SO_REUSEADDR socket option for the server's listening socket
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("Failed to set the SO_REUSEADDR socket option.");
        exit(1);
    }

    // Initialize the server address structure
    memset(&server_address, 0, addr_size); // Zero out the structure to ensure it's clean before use
    server_address.sin_family = AF_INET; // Specify the address family (IPv4)
    // argv[1]: The port number for the server to bind to (expected to be a 16-bit unsigned integer)
    server_address.sin_port = htons(atoi(argv[1])); // Convert the port number from host to network byte order
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on all interfaces

    // Bind the server's listening socket to the specified port and IP address contained in server_address
    if (bind(sock_fd, (struct sockaddr *) &server_address, addr_size) < 0) {
        perror("Failed to bind the listening socket to the specified IP address and port.");
        exit(1);
    }

    // Configure the server's socket to listen for incoming connections
    if (listen(sock_fd, 10) < 0) {
        perror("Failed to configure the server socket to listen mode.");
        exit(1);
    }

    // Loop to continuously accept incoming client connections
    while (1) {
        // Accept an incoming client connection
        int new_sock_fd = accept(sock_fd, (struct sockaddr *) &client_address, &addr_size);
        if (new_sock_fd < 0) {
            perror("Failed to accept a new client connection.");
            exit(1);
        }

        // Currently interacting with a client
        PROCESSING_CLIENT = 1;
        cont_to_next_clnt = 0;

        // Initiate the process of receiving the file size from the connected client
        total_bytes_read = 0;
        // Read data from the client until the expected amount of bytes for the file size has been received
        while (total_bytes_read < sizeof(file_size_network)) {
            bytes_read = read(new_sock_fd, (char *)(&file_size_network) + total_bytes_read,
                              sizeof(file_size_network) - total_bytes_read);

            // Handle read errors or client disconnection
            if (bytes_read <= 0) {
                if (bytes_read == 0 || errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE) {
                    cont_to_next_clnt = 1; // Mark to continue to next client due to disconnection
                    break;
                }
                perror("Failed to read the file size from the client.");
                exit(1);
            }
            total_bytes_read += bytes_read;
        }

        // Continue to the next client if needed
        if (cont_to_next_clnt) {
            handle_client(new_sock_fd);
            continue; // Move on to accept a new client connection
        }

        // Convert the received file size from network byte order to host byte order
        file_size_host = ntohl(file_size_network);

        // Receive the file content from the client
        total_bytes_read = 0;
        total_printable_chars_host = 0;
        memset(client_pcc, 0, sizeof(client_pcc)); // Zero out the character count array

        // Read the file content in chunks until the entire file is received
        while (total_bytes_read < file_size_host) {
            bytes_read = read(new_sock_fd, buffer, BUFF_SIZE);

            // Handle read errors or client disconnection
            if (bytes_read <= 0) {
                if (bytes_read == 0 || errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE) {
                    cont_to_next_clnt = 1; // Mark to continue to next client due to disconnection
                    break;
                }
                perror("Failed reading file content from socket.");
                exit(1);
            }

            total_bytes_read += bytes_read;

            // Count printable characters in the received chunk
            for (i = 0; i < bytes_read; i++) {
                if (buffer[i] >= 32 && buffer[i] <= 126) {
                    client_pcc[(unsigned int) buffer[i]]++;
                    total_printable_chars_host++;
                }
            }
        }

        // Continue to the next client if needed
        if (cont_to_next_clnt) {
            handle_client(new_sock_fd);
            continue; // Move on to accept a new client connection
        }

        // Send the count of printable characters back to the client
        total_bytes_sent = 0;
        // Convert the total count of printable characters from host to network byte order
        total_printable_chars_network = htonl(total_printable_chars_host);

        // Continues sending data until the entire count of printable characters has been transmitted
        while (total_bytes_sent < sizeof(total_printable_chars_network)) {
            bytes_sent = write(new_sock_fd, (char *)(&total_printable_chars_network) + total_bytes_sent,
                               sizeof(total_printable_chars_network) - total_bytes_sent);

            // Handle write errors or client disconnection
            if (bytes_sent < 0) {
                if (errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE) {
                    cont_to_next_clnt = 1; // Mark to continue to next client due to disconnection
                    break;
                }
                perror("Failed to send the count of printable characters to the client.");
                exit(1);
            }

            total_bytes_sent += bytes_sent;
        }

        // Continue to the next client if needed
        if (cont_to_next_clnt) {
            handle_client(new_sock_fd);
            continue; // Move on to accept a new client connection
        }

        // Update total count of printable characters
        for (i = 32; i < 127; i++)
            total_pcc[i] += client_pcc[i];

        // Close the current client's connection socket
        close(new_sock_fd);

        // Reset the flag to indicate the server is no longer processing a client request
        PROCESSING_CLIENT = 0;

        // Check if a signal to terminate the server was received during client processing
        if (TERMINATE == 1)
            // Initiate server shutdown sequence if termination flag is set
            shutdown_server();
    }
}