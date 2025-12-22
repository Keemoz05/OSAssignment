#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>



int main(){

    // variable declarations //
    int fd;

    // buffer to hold player name with sufficient size //
    char player_name[100];


    // open named pipe file for reading and writing //
    fd = open("player_pipe" , O_RDWR);

    // enter your name //
    printf("Enter your name:");

    // read player name from standard input //
    fgets(player_name , sizeof(player_name) , stdin);

    // write player name to pipe //
    write(fd , player_name , strlen(player_name));

    // close the pipe //
    close(fd);

     // display the message confirming the player has joined //
    printf("Player %s has joined the game\n" , player_name);

    while(1); // keep player terminal alive


    
    
}