#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#define MAX_PLAYERS 5
#define PLAYER_NAME_SIZE 20

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

// Each player gets one of these 'structs'
typedef struct {
    char guesses[100][20]; // Can hold 100 words, max 20 letters each
    int guess_count;       // Keeps track of how many guesses they made
} PlayerHistory;

int main(){
    
    int player_amount; // for number of players 
    char buffer[PLAYER_NAME_SIZE]; // buffer is just temporary data storage 
    char player_names[MAX_PLAYERS][PLAYER_NAME_SIZE]; // array to store player names
    int count = 0 ;
    PlayerHistory *all_players_data;
    printf("Enter number of players (max %d): ", MAX_PLAYERS);
    scanf("%d", &player_amount);

        // open player terminals
    for (int i = 0; i < player_amount; i++) {
        if (fork() == 0) {
            char title[20];
            sprintf(title, "Client %d", i + 1);
            execlp("xterm", "xterm", "-T", title,
                   "-e", "./client", NULL);
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
        
        // Check if lobby is full
        if (count == player_amount) {

            //GAME INIT HERE//
            //==============================================================================//
            printf("%d Players have entered the game! Starting...\n", player_amount);

            //Create a dynamic list of lists(3d array) that store each player input(guesses), 
            //if 3 players,create 3 list which will contain strings

            // 1. Allocate memory: Create a list for EACH player
            all_players_data = malloc(player_amount * sizeof(PlayerHistory));

            // 2. Initialize: Set everyone's guess count to 0 to start
            for(int i = 0; i < player_amount; i++) {
                all_players_data[i].guess_count = 0;
            }
            break;
            //==============================================================================//
        }
    }
}

    //---------------------------------------------------------------------------------------

    while(1){
        printf("Lets start!\n");

        sleep(10);
    }

    } 

 