#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h> //added this for logging the activities
#include "wordbank.h"
//NOTE: ALWAYS END PRINTF WITH NEWLINE TO AVOID BUFFER/INPUT HALT ISSUES, THERES A REASON SOMEWHERE BUT IM TOO LAZY TO FIND IT
#define SIGNAL_GAME_START 1
#define SIGNAL_YOUR_TURN  2
#define SIGNAL_GAME_OVER  3
#define SIGNAL_RESULT 4
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
    printf("%s sent!\n", player_name);

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
            //SEND THE GUESS TO THE SERVER:done
            //SERVER THEN WILL SAVE THE GUESS INTO A STRUCT BASED ON PLAYER ID : done
            //THEN SERVER WILL PROCESS THE GUESS AND SEND BACK THE RESULT TO THE CLIENT
            //CLIENT WILL THEN DISPLAY THE RESULT TO THE PLAYER
            //CLIENT WILL WAIT FOR NEXT TURN SIGNAL FROM SERVER
            //======================================================================================


            //CODE SEGMENT TO PROCESS SIGNALS FROM SERVER

            switch(signal) {
                case SIGNAL_YOUR_TURN:
                    printf("\n[IT IS YOUR TURN]\n");
                    do {
                        printf("Enter your guess (5 letters): "); // Added prompt text
                        fgets(guess, sizeof(guess), stdin);

                        // FIX: Remove the newline character if it exists
                        guess[strcspn(guess, "\n")] = 0; 

                    // FIX: Check length. If you want exactly 5 letters, use != 5
                    } while(strlen(guess) != 5); 

                    fd = open("player_pipe", O_WRONLY);
                    if (fd < 0){
                        perror("Failed to open pipe");
                        exit(1);
                    }

                    // Send ID
                    int id_to_send = atoi(my_id);
                    write(fd, &id_to_send, sizeof(int));


                    write(fd, guess, strlen(guess) + 1); // +1 sends the null terminator too
                    
                    printf("Sent guess: %s\n", guess);
                    //server process guess
                    //server produces a variable called output which will be passed to client terminal


                    //close(fd);

                    break;
                case SIGNAL_RESULT:
                char result_string[10]; 
                
                // Read the actual string data immediately after the signal
                read(my_mailbox, result_string, 6); 
                
                printf("\n-----------------------------\n");
                printf("Your Guess: %s\n", result_string);
                printf("-----------------------------\n");
                printf("Waiting for other players to finish... ")
                break;
            //END OF CODE SEGMENT TO PROCESS SIGNALS FROM SERVER
        }
            }
    
    }
}
