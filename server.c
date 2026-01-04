#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h> //mkfifo 
#include <signal.h> 
#include <time.h> //added this for logging the activities within the server
#include "wordbank.h"

#define MAX_PLAYERS 5
#define PLAYER_NAME_SIZE 20

//NOTE: ALWAYS END PRINTF WITH NEWLINE TO AVOID BUFFER/INPUT HALT ISSUES, THERES A REASON SOMEWHERE BUT IM TOO LAZY TO FIND IT

// Define these at the top of both Client and Server
#define SIGNAL_GAME_START 1
#define SIGNAL_YOUR_TURN  2
#define SIGNAL_GAME_OVER  3
//ignore this function, sometimes stupid buffer got hal and will make 'Enter' key not work

//Usage: debug_buffer(buffer,100)
void debug_buffer(char *b, int size) {
    printf("--- Buffer Content ---\n");
    for (int i = 0; i < size; i++) {
        // Print the character if it's printable (like 'A' or '9')
        if (b[i] >= 32 && b[i] <= 126) {
            printf("[%c] ", b[i]);
        } 
        // Print special names for invisible chars
        else if (b[i] == '\0') printf("[\\0]");
        else if (b[i] == '\n') printf("[\\n]");
        // Print '?' for anything else (garbage)
        else printf("[?]");
    }
    printf("\n----------------------\n");
}

//For the activity logs----------------------------------------------
void log_event(FILE *fptr, const char *event)
{
    time_t now = time(NULL);
    char timebuf[64];

    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(fptr, "[%s] %s\n", timebuf, event);
    fflush(fptr);
}
//---------------------------------------------------------------

// Each player gets one of these 'structs'
typedef struct {
    char guesses[100][20]; // Can hold 100 words, max 20 letters each
    int guess_count;       // Keeps track of how many guesses they made
} PlayerHistory;

int main(){
    FILE *fptr; // init a file pointer //
    int player_amount; // for number of players 
    int game_session = 0; // to track game sessions //
    char buffer[PLAYER_NAME_SIZE]; // buffer is just temporary data storage 
    char player_names[MAX_PLAYERS][PLAYER_NAME_SIZE]; // array to store player names
    int count = 0 ;
    PlayerHistory *all_players_data;


    // read the last game session from file //
    fptr = fopen("player.txt" , "r");
    if (fptr != NULL){
        char line[256];
        int session;
        while (fgets(line , sizeof(line) , fptr)){
            if (sscanf(line , "========================= Game %d=============================" , &session) == 1){
                game_session = session;
            }
        }

        fclose(fptr);
    }

    game_session++; // increment game session for new game //


    if(access("player.txt" , F_OK) == 0){ // check if file exists //
        fptr = fopen("player.txt" , "a");  // open file in append mode only //
    } else {
        fptr = fopen("player.txt" , "w"); // create new file if not exists //
        
    }

    if(fptr == NULL){
    perror("player.txt");
    exit(1);
}

    
    fprintf(fptr , "========================= Game %d=============================\n" , game_session);
    log_event(fptr, "Game session started"); //logs the activity into a file
    fflush(fptr);
   
    do{ 
        
        printf("Enter number of players (max %d): ", MAX_PLAYERS);
        scanf("%d", &player_amount);
        char buffer[50];                                               // creating a buffer to write the data into //
        sprintf(buffer , "Player amount: %d\n" , player_amount);       // writing the data into the buffer //
        fputs(buffer , fptr );                   // writing the buffer data into the file //
        fflush(fptr);                        // flushing the file to ensure data is written //

        //for the logging activity----------------------------------------------------
        char logbuf_amount[100];
        sprintf(logbuf_amount, "Player amount selected: %d", player_amount);
        log_event(fptr, logbuf_amount);
        //----------------------------------------------------------------------------
    }


    while (player_amount > MAX_PLAYERS || player_amount <= 0); 
    //so nobody creates 2000 client terminals for the funsies

    //create shared lobby pipe
    mkfifo("player_pipes", 0666);

    //named pipe for each player process
    for (int i = 0; i < player_amount; i++) {
            char pipe_name[20];
            sprintf(pipe_name, "p%d", i + 1);  //p1,
            mkfifo(pipe_name , 0666);

               
        }    
    

        // open player terminals
    for (int i = 0; i < player_amount; i++) {
        if (fork() == 0) {
            char title[20]; 
            char player_id[5];

            //sprintf converts it into string and adds one so it does not start from zero
            sprintf(title, "Client %d", i + 1);
            sprintf(player_id,"%d",i+1);

            //can we not just use printf vro </3, allat to print some lines | DO NOT CHANGE IT INTO PRINTF, TURN WONT START AND CLIENT TERMINAL NAME WILL CHANGE, I TRIED

            //execlp only accepts strings as arguments, so pass in title and player_id to each client as arguments
            execlp("xterm", "xterm", "-T", title,"-e", "./client",player_id, NULL); //this creates a client terminal, passing the id number
            exit(1);
        }
    }

    
    // open named pipe file for reading //
    int fd = open("player_pipe", O_RDWR);

    // error handling if pipe fails to open //
    if ( fd < 0){
        perror("Failed to open pipe");
        exit(1);
    }
   
    //-----------------------------------------------------------------------------------
    //WAITING FOR PLAYERS PHASE 
while (1) {
    char ch;
    int index = 0;

    // Read each character until encounter "ENTER"
    while (read(fd, &ch, 1) > 0) {
        if (ch == '\n') {
            buffer[index] = '\0'; // Finish the string
            break; // Break the inner reading loop, process the name
        } 
        else if (index < PLAYER_NAME_SIZE - 1) {
            buffer[index++] = ch; // Store char and increment index
        }
    }

    
    
    // What it does:

    // if (index > 0): This ignores empty lines. If a user just hits "Enter" without typing a name, index will be 0, and we skip this logic.

    // strcpy(...): Copies the name from our temporary buffer into the permanent player_names list.

    // count++: We have one more player!

    if (index > 0) {
        strcpy(player_names[count], buffer);
        count++;

        printf("Player %s has joined the game\n", player_names[count-1]);

        //For logging activities
        char logbuf_join[150];
        sprintf(logbuf_join, "Player joined: ID=%d Name=%s", count, player_names[count-1]);
        log_event(fptr, logbuf_join);
        
        // Check if lobby is full
        if (count == player_amount) {  

            //IF lobby is full, START THE GAME //
//========================================================================================================================================================================//
            
            printf("%d Players have entered the game! Starting...\n", player_amount);
            log_event(fptr, "All players joined. Game starting.");//Logs this activity

            //allocate memory
            all_players_data = malloc(player_amount * sizeof(PlayerHistory));
            for(int i = 0; i < player_amount; i++) {
                all_players_data[i].guess_count = 0;
            }

            int player_write_fds[MAX_PLAYERS];  

            //open each players pipehole ;)
            for (int i = 0; i < player_amount; i++) {
                char pipe_name[20];
                sprintf(pipe_name, "p%d", i + 1); 

                // Open as WRONLY (Write Only) because Server only talks here
                player_write_fds[i] = open(pipe_name, O_WRONLY);
            }

            //OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO

            //OBJECTIVE:
            //TELL ALL CLIENTS THAT THE GAME IS STARTING
            //TELL P1 IT IS HIS TURN NOW

            //THE BAT signal to game start for all clients
            for (int i = 0; i < player_amount; i++) {
                int message = SIGNAL_GAME_START;
                write(player_write_fds[i], &message, sizeof(int));
            }

            //wait a bit
            sleep(1);

            int message = SIGNAL_YOUR_TURN;
            write(player_write_fds[0], &message, sizeof(int)); 

            close(fd);
            break; // Break the lobby loop, which will move to the game phase

            //OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO

            

            

            //error checks to be implemented

            //Create a dynamic list of lists(3d array) that store each player input(guesses), 
            //if 3 players,create 3 list which will contain strings

            // 1. Allocate memory: Create a list for EACH player
           //this section is moved before the break



    
            
            //==============================================================================//
        }
    }
}

    //---------------------------------------------------------------------------------------

    while(1){

        int fd = open("player_pipe", O_RDWR);
        //printf("Game is running.\n");

        //sleep(20);
        int playerid;
        char guess_buffer[50];

        read(fd, &playerid, sizeof(int));
        
        read(fd, guess_buffer, sizeof(guess_buffer) - 1);

        printf("Player %d guessed: %s\n", playerid, guess_buffer);

        //Logs the activity....
        char logbuf_guess[200];
        sprintf(logbuf_guess, "Guess received from Player %d: %s", playerid, guess_buffer);
        log_event(fptr, logbuf_guess);

        printf("approaching count section\n");

        all_players_data[playerid].guess_count =  all_players_data[playerid].guess_count + 1;
        
    
        close(fd);
        break;
    }

    log_event(fptr, "Game ended. Printing guess statistics.");//logs the end of the game

    printf("\n--- Guess Tracker ---\n");
    for(int i = 0; i < player_amount; i++) {
    printf("ID: %d | Name: %s | Total Guesses: %d\n", 
            i + 1, 
            player_names[i], 
            all_players_data[i].guess_count);
    }
    printf("------------------\n");
    } 

 
