#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

int main(int argc, char *argv[]) {
    // variable declarations //
    // 1. Get my ID from the arguments passed by Server
    if (argc < 2) {
        printf("Error: Run this via the server!\n");
        return 1;
    }
    char *my_id = argv[1]; // my_id will contain the playerr_id argumetn from the server


    int fd;

    // buffer to hold player name with sufficient size //
    char player_name[100];

    // open named pipe file for reading and writing //
    fd = open("player_pipe", O_RDWR);

    // enter your name //
    printf("Enter your name:");

    // read player name from standard input //
    fgets(player_name, sizeof(player_name), stdin);

    //find the \n and cuts it off, try removing this
    //player_name[strcspn(player_name, "\n")] = 0;

    // write player name to pipe //
    write(fd, player_name, strlen(player_name));

    // close the pipe //

    // display the message confirming the player has joined //
    printf("Player %s has joined the game\n", player_name);


    // 2. Open my personal mailbox for READING
    char my_pipe_name[20];
    sprintf(my_pipe_name, "p%s", my_id); // becomes "p1", "p2"...

    // IMPORTANT: Client opens this as RDONLY (Read Only)
    int my_mailbox = open(my_pipe_name, O_RDONLY);
 while(1) {
        char buffer[100];
        int n = read(my_mailbox, buffer, 99);
        if (n > 0) {
            buffer[n] = '\0'; // Clean string
            
            //instead of using strings, what are better ways to signal game start and your turn?

            //OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO
            //OBJECTIVE:
            //RECEIVE THE SIGNAL FROM SERVER THAT THE GAME IS STARTING
            //RECEIVE THE SIGNAL FROM SERVER THAT IT IS YOUR TURN NOW
            if (strstr(buffer, "GAME_START") != NULL) {
                printf("All players have entered. Game is starting!\n");
            }

            // Check if "YOUR_TURN" exists ANYWHERE in the buffer
            if (strstr(buffer, "YOUR_TURN") != NULL) {
                printf("It's your turn! Enter guess: ");
                
                // Logic to send guess...
            }

            //OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO
        }
    }
}