#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h> //mkfifo 
#include <signal.h> 
#include <ctype.h> //toupper
#include <time.h> //added this for logging the activities within the server
#include "wordbank.h"

#define MAX_PLAYERS 5
#define PLAYER_NAME_SIZE 20
#define WORD_LENGTH 5
//NOTE: ALWAYS END PRINTF WITH NEWLINE TO AVOID BUFFER/INPUT HALT ISSUES, THERES A REASON SOMEWHERE BUT IM TOO LAZY TO FIND IT

// Define these at the top of both Client and Server
#define SIGNAL_GAME_START 1
#define SIGNAL_YOUR_TURN  2
#define SIGNAL_GAME_OVER  3
#define SIGNAL_RESULT     4




const char *random_word = NULL; //rename the word to random_word and make it global
int player_write_fds[MAX_PLAYERS];
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

// function to PROCESS the guess and return wordle-like results
void evaluate_guess(char guess[],const char target[], char result[]) {
    
    // 1. Setup variables (Standard integers and arrays)
    // letter_budget keeps track of how many of each letter are available in target word so that yellows and grays can be determined correctly
    int letter_budget[26] = {0}; 
    int i; 

    // Initialize result array with all 'X' (Gray)
    // We can use a simple loop instead of memset to avoid pointer-based functions
    for (i = 0; i < WORD_LENGTH; i++) {
        result[i] = 'X';
    }
    result[WORD_LENGTH] = '\0'; // End the string

    // 2. Build the Budget 
    for (i = 0; i < WORD_LENGTH; i++) {
        // Converts char to 0-25 index (A=0, B=1, etc.)
        int target_index = toupper(target[i]) - 'A';
        letter_budget[target_index]++;
    }

    // 3. PASS ONE: Green (Exact Matches)
    for (i = 0; i < WORD_LENGTH; i++) {
        char guess_char = toupper(guess[i]);
        char target_char = toupper(target[i]);

        if (guess_char == target_char) {
            result[i] = 'G';
            
            int letter_index = guess_char - 'A';
            letter_budget[letter_index]--; 
        }
    }

    // 4. PASS TWO: Yellow (Wrong Position)
    for (i = 0; i < WORD_LENGTH; i++) {
        char guess_char = toupper(guess[i]);
        int letter_index = guess_char - 'A';

        // Skip if we already marked it Green
        if (result[i] == 'G') {
            continue;
        }

        // Check budget
        if (letter_budget[letter_index] > 0) {
            result[i] = 'Y';
            letter_budget[letter_index]--;
        }
       
    }
}
//For the activity logs----------------------------------------------
void log_event(FILE *fptr, const char *event)
{
    if (fptr == NULL) return ;
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
   
    srand(time(NULL));
    do{ 
        //randomly pick a word
        
        int word_index = rand() % word_count;
        
        random_word = word_bank[word_index];
        
        printf("Server selected word: %s\n", random_word); //this is just to check if server actually got a randomised word

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


    //PIPE CREATION : player_pipe acts as the "broadcast" pipe for all players to send data to server, p1,p2,p3,
    // are pipes for server to send data to each player respectively

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
            char player_id[30];

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

            //int player_write_fds[MAX_PLAYERS]; //this holds the ineger ID for each player's pipe, moved to global variable

            //open each players pipehole ;)
            for (int i = 0; i < player_amount; i++) {
                char pipe_name[20];
                sprintf(pipe_name, "p%d", i + 1); 

                player_write_fds[i] = open(pipe_name, O_WRONLY);
            }
            //player_write_fds[0] is p1, player_write_fds[1] is p2, open() takes string of pipe_name and returns a number
            //So if you wanna pass a message to p2, you do write(player_write_fds[1], "masrusdi", strlen("masrusdi"));

            //=============LOBBY SETUP PHASE START ===================================//

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

            //LOBBY SETUP PHASE END===================================//

            

            

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
        
        read(fd, guess_buffer, sizeof(guess_buffer) - 1); //store the guess into guess_buffer

        printf("Player %d guessed: %s\n", playerid, guess_buffer);

       //use array_index to access the correct player's struct, sebab our player IDs start from 1 but array index starts from 0
        int array_index = playerid - 1;

        // error check to prevent out-of-bounds access, try remove later and see what happens
        if (array_index < 0 || array_index >= player_amount) {
            continue;
        }

         //store the guess into the player's struct 
        strcpy(all_players_data[array_index].guesses[all_players_data[array_index].guess_count], guess_buffer); //
        //printf("Stored guess for Player %d: %s\n", playerid, all_players_data[playerid].guesses[all_players_data[playerid].guess_count]);

        //==========LOGIC TO PROCESS THE GUESS==========

        char output[6];
        evaluate_guess(guess_buffer, random_word, output);
        printf("Result: %s\n", output);
        //pass the output to the client
        int result_sig = SIGNAL_RESULT;

        // signal the client first
        write(player_write_fds[array_index], &result_sig, sizeof(int));

        //then bagi the output 
        write(player_write_fds[array_index], output, sizeof(output));



        //=================================================




        //processguess(playerid, guess_buffer); //



        //Logs the activity....
        char logbuf_guess[200];
        sprintf(logbuf_guess, "Guess received from Player %d: %s", playerid, guess_buffer);
        log_event(fptr, logbuf_guess);



        all_players_data[array_index].guess_count++;
        
        int next_player_id = (playerid % player_amount) + 1;

        printf("Pass turn to Player %d\n", next_player_id);


        // ==========================================================
        // STEP 2: Construct the Pipe Name for the Next Player
        // ==========================================================
        char next_pipe_name[20];
        sprintf(next_pipe_name, "p%d", next_player_id);


        // ==========================================================
        // STEP 3: Signal the Next Player
        // ==========================================================
        int next_fd = open(next_pipe_name, O_WRONLY);
        if (next_fd < 0) {
            perror("Failed to open next player's pipe");
            // Don't exit here, or the server dies. Just continue.
        } else {
            int signal = SIGNAL_YOUR_TURN; // Value is 2
            write(next_fd, &signal, sizeof(int));
            close(next_fd); // vital to close this immediately
            printf("Signal sent to %s\n", next_pipe_name);
        }
        close(fd);
        //break;
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

 