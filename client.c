#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

//NOTE: ALWAYS END PRINTF WITH NEWLINE TO AVOID BUFFER/INPUT HALT ISSUES, THERES A REASON SOMEWHERE BUT IM TOO LAZY TO FIND IT
#define SIGNAL_GAME_START 1
#define SIGNAL_YOUR_TURN  2
#define SIGNAL_GAME_OVER  3

int main(int argc, char *argv[]) {
    // variable declarations //
    char guess[51];
    

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
    //fd = open("player_pipe", O_RDWR);

   

    // enter your name //
    printf("Enter your name:");

    // read player name from standard input //
    fgets(player_name, sizeof(player_name), stdin);

    //find the \n and cuts it off, try removing this
    //player_name[strcspn(player_name, "\n")] = 0;

    // write player name to pipe //
    fd = open("player_pipe", O_WRONLY);

      // error handling if pipe fails to open //
    if ( fd < 0){
        perror("Failed to open pipe");
        exit(1);
    }

    write(fd, player_name, strlen(player_name));
    printf(player_name, " sent!");

    close(fd);

    // close the pipe //

    // display the message confirming the player has joined //
    printf("Player %s has joined the game\n", player_name);


    // 2. Open my personal mailbox for READING
    char my_pipe_name[20];
    sprintf(my_pipe_name, "p%s", my_id); // becomes "p1", "p2"...

    // IMPORTANT: Client opens this as RDONLY (Read Only)
    int my_mailbox = open(my_pipe_name, O_RDONLY);
    while(1) {
        int signal;
        int n = read(my_mailbox, &signal, sizeof(int));
        if (n > 0) {
            //instead of using strings, what are better ways to signal game start and your turn?

            //======================================================================================
            //OBJECTIVE:
            //RECEIVE THE SIGNAL FROM SERVER THAT THE GAME IS STARTING: done
            //RECEIVE THE SIGNAL FROM SERVER THAT IT IS YOUR TURN NOW:done
            //SEND THE GUESS TO THE SERVER
            //SERVER THEN WILL SAVE THE GUESS INTO A STRUCT BASED ON PLAYER ID
            //THEN SERVER WILL PROCESS THE GUESS AND SEND BACK THE RESULT TO THE CLIENT
            //======================================================================================


            //CODE SEGMENT TO PROCESS SIGNALS FROM SERVER

            switch(signal) {
                case SIGNAL_GAME_START:
                    printf("\n>>> GAME STARTING! <<<\n");
                    break;


                case SIGNAL_YOUR_TURN:
                    printf("\n[IT IS YOUR TURN]\n");
                    printf("Enter your guess:\n");
                    fgets(guess, sizeof(guess), stdin);

                    printf(my_id,"\n");
                    printf("opening\n");

                    fd = open("player_pipe", O_WRONLY);
                      // error handling if pipe fails to open //
                    if ( fd < 0){
                        perror("Failed to open pipe");
                        exit(1);
                    }

                    //send id
                    int id_to_send = atoi(my_id);
                    write(fd, &id_to_send, sizeof(int));

                    //send guess
                    write(fd, guess, strlen(guess));
                    
                    printf(" sent guess: %s packet!\n", guess);

                    
                    close(fd);
                

                    //printf(guess,"\n");
                    
                    // char guess_buffer[100];
                    // fgets(guess_buffer, sizeof(guess_buffer), stdin);
                    // guess_buffer[strcspn(guess_buffer, "\n")] = 0; // Clean newline

                    // // LOGIC TO SEND GUESS:
                    // // We write the guess back to the main server_fd.
                    // // Note: Depending on your server logic, you might want to 
                    // // prepend your ID, e.g., "1:Apple", so the server knows it's you.
                    // // For now, we just send the raw guess.
                    // write(server_fd, guess_buffer, strlen(guess_buffer));
                    
                    // printf("Guess sent: %s\n", guess_buffer);
                    // printf("Waiting for other players...\n");
                    break;
            }
            //END OF CODE SEGMENT TO PROCESS SIGNALS FROM SERVER
        }
    }
}
