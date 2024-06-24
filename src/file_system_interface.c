#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

// File System Interface
// This component provides an abstraction layer for interacting with the underlying file system.

void file_system_interface() {
    int fd;
    char buffer[256];
    ssize_t bytes_read;

    // Open a file
    fd = open("example.txt", O_RDONLY);
    if (fd == -1) {
        perror("Error opening file");
        return;
    }

    // Read from the file
    bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read == -1) {
        perror("Error reading file");
        close(fd);
        return;
    }

    // Null-terminate the buffer and print the contents
    buffer[bytes_read] = '\0';
    printf("File contents: %s\n", buffer);

    // Close the file
    if (close(fd) == -1) {
        perror("Error closing file");
        return;
    }
}

int main() {
    // Test the file_system_interface function
    file_system_interface();
    return 0;
}
