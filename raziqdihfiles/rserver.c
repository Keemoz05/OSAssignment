#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#define MAX_PLAYERS 10
#define PLAYER_NAME_SIZE 20

int main(){
    
    int players; // for number of players 
    char buffer[PLAYER_NAME_SIZE]; // buffer to hold player name 
    char player_names[MAX_PLAYERS][PLAYER_NAME_SIZE]; // array to store player names
    int count = 0 ;

  

  

    // open named pipe file for reading //
    int fd = open("player_pipe", O_RDWR);

    // error handling if pipe fails to open //
    if ( fd < 0){
        perror("Failed to open pipe");
        exit(1);
    }

    // display server started message //
    printf("Server started, waiting for players...\n");

    printf("Waiting for player...\n");

    while (1){

        // read player name from pipe //

        int bytes = read(fd , buffer , PLAYER_NAME_SIZE);

        if (bytes > 0){

            // this is to remove newline character from end of the string //
            buffer[bytes  - 1] = '\0';

            // store player name and display message //
            if (count < MAX_PLAYERS){
                // copy player name to array //
                strcpy(player_names[count] , buffer);
                printf("Player %s has joined the game\n" , player_names[count]);
                count++;


                // display list of joined players //
                printf("\nPlayer Joined List:\n");
                for ( int i = 0 ; i < count ; i++){
                    printf("Player %d: %s\n" , i + 1 , player_names[i]);
                }

                // display total number of players //
                printf("Total Players:");
                printf("%d\n" , count);
            }
        }
    }

    } 

 