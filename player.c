#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#define SERVER_PIPE "game_pipe"

int main() {
    int fd_server, fd_my_pipe;
    int my_id, my_guess;
    char buffer[100]; // Receiving from server
    char send_buf[100]; // Sending to server
    char my_pipe_name[20];

    printf("Enter Player ID (1, 2, or 3): ");
    scanf("%d", &my_id);

    // Create my listening pipe
    sprintf(my_pipe_name, "player%d", my_id);
    mkfifo(my_pipe_name, 0666);

    printf("Player %d ready. Waiting for Server signal...\n", my_id);

    // Open my pipe specifically for reading (Blocks here until someone writes!)
    fd_my_pipe = open(my_pipe_name, O_RDWR); // O_RDWR keeps it open even if empty

    while (1) {
        // 1. Wait for instruction from Server
        memset(buffer, 0, sizeof(buffer)); // Clear buffer
        read(fd_my_pipe, buffer, sizeof(buffer));

        // 2. Decide what to do based on message
        if (strcmp(buffer, "GO") == 0) {
            // It is my turn!
            printf("\n>> IT IS YOUR TURN! <<\nEnter guess: ");
            scanf("%d", &my_guess);

            // Send guess
            sprintf(send_buf, "%d %d", my_id, my_guess);
            fd_server = open(SERVER_PIPE, O_WRONLY);
            write(fd_server, send_buf, strlen(send_buf) + 1);
            close(fd_server);
            
            printf("Guess sent. Waiting for results...\n");

        } else {
            // It's just a message (Win alert, or "Too High")
            printf("\n[SERVER]: %s\n", buffer);
        }
    }

    return 0;
}