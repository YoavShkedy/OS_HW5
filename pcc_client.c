#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // inet_pton(): IP address to binary representation
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

const int BUFF_SIZE = 1000 * 1000; // Approximately 1MB

int main(int argc, char *argv[]) {
    // argv[1]: Server's IP address (must be a valid IPv4 address format)
    // argv[2]: Server's port number (must be a valid 16-bit unsigned integer, in the range of 0 to 65535)
    // argv[3]: Path of the file to send to the server

    int fd; // File descriptor for the file to be sent
    int sock_fd; // Socket file descriptor for TCP connection to the server
    char file_buffer[BUFF_SIZE]; // Buffer for temporarily storing file content during read and send operations
    struct stat file_info; // Structure to store information about the file, such as size

    unsigned int file_size; // Size of the file to be sent, in bytes
    unsigned int file_size_network; // File size converted to network byte order for transmission
    unsigned int printable_chars_cnt; // Count of printable characters received from the server
    unsigned int printable_chars_cnt_network; // Printable characters count in network byte order

    ssize_t bytes_read;
    ssize_t bytes_sent;
    ssize_t total_bytes_sent;
    ssize_t bytes_to_receive;
    ssize_t bytes_received;

    struct sockaddr_in server_address; // Structure to store server's address information for the connection

    // Validate the number of command-line arguments
    if (argc != 4) {
        perror("Error: Exactly 3 arguments are required: <Server's IP> <Server's Port> <File Path>.");
        exit(1);
    }

    // Open the specified file in read-only mode
    fd = open(argv[3], O_RDONLY);
    if (fd < 0) {
        perror("Failed to open the file.");
        exit(1);
    }

    // Retrieve the size of the file
    if (fstat(fd, &file_info) == 0)
        file_size = (unsigned int) file_info.st_size;
    else {
        perror("Failed to obtain the size of the file.");
        exit(1);
    }

    // Initialize a TCP socket for IPv4 communication
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Failed to create a TCP socket.");
        exit(1);
    }

    // Initialize the server address structure
    memset(&server_address, 0, sizeof(struct sockaddr_in)); // Zero out the structure to ensure it's clean before use
    server_address.sin_family = AF_INET; // Specify the address family (IPv4)
    server_address.sin_port = htons(atoi(argv[2])); // Convert the port number from host to network byte order
    // Convert the server's IP address from text to binary form
    // Store it in the server_address structure for socket communication
    if (inet_pton(AF_INET, argv[1], &(server_address.sin_addr)) < 1) {
        perror("Failed to set the server's IP address.");
        exit(1);
    }

    // Establish a connection to the server
    if (connect(sock_fd, (struct sockaddr *) &server_address, sizeof(struct sockaddr_in)) < 0) {
        perror("Failed to connect to the server");
        exit(1);
    }

    // Prepare to send the file size to the server
    total_bytes_sent = 0;
    file_size_network = htonl(file_size); // Convert the file size to network byte order

    // Loop to ensure the entire file size is sent to the server
    while (total_bytes_sent < sizeof(file_size_network)) {
        // Attempt to send the remaining portion of the file size
        bytes_sent = write(sock_fd, (char *)(&file_size_network) + total_bytes_sent, sizeof(file_size) - total_bytes_sent);
        if (bytes_sent < 0) {
            perror("Failed to send the file size to the server.");
            exit(1);
        }
        total_bytes_sent += bytes_sent;
    }

    // Send the entire file's content to the server in manageable chunks
    total_bytes_sent = 0;
    while (total_bytes_sent < file_size) {
        // Clear the buffer for the next chunk of data
        memset(file_buffer, 0, BUFF_SIZE);

        // Read a chunk of the file into the buffer
        bytes_read = read(fd, file_buffer, BUFF_SIZE);
        if (bytes_read < 0) {
            perror("Failed to read from the file.");
            exit(1);
        }

        // Send the chunk of data read from the file to the server
        bytes_sent = 0;
        while (bytes_read > 0) {
            // Attempt to send the current chunk of data to the server
            bytes_sent = write(sock_fd, file_buffer + bytes_sent, bytes_read - bytes_sent);
            if (bytes_sent < 0) {
                perror("Failed to send the file to the server.");
                exit(1);
            }

            // Update counters based on the amount of data successfully sent
            total_bytes_sent += bytes_sent;
            bytes_read -= bytes_sent;
        }
    }

    // Close the file descriptor
    close(fd);

    // Initiate the process of receiving the count of printable characters from the server
    bytes_to_receive = sizeof(unsigned int); // The expected size of the data to receive
    bytes_received = 0; // Bytes received so far
    while (bytes_received < bytes_to_receive) {
        // Read the incoming bytes representing the count of printable characters
        bytes_read = read(sock_fd, (char *)(&printable_chars_cnt_network) + bytes_received, bytes_to_receive - bytes_received);
        if (bytes_read < 0) {
            perror("Failed to receive the count of printable characters from the server.");
            exit(1);
        }
        bytes_received += bytes_read;
    }

    // Convert the count from network byte order to host byte order and print it
    printable_chars_cnt = ntohl(printable_chars_cnt_network);
    printf("# of printable characters: %hu\n", printable_chars_cnt);

    // Close the socket descriptor
    close(sock_fd);

    exit(0);
}