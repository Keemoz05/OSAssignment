#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SERVER_PIPE "game_pipe"

void send_to_player(int id, char *msg) {
    char pipe_name[20];
    sprintf(pipe_name, "player%d", id);
    int fd = open(pipe_name, O_WRONLY);
    // Use write with fixed size or handle length carefully
    write(fd, msg, strlen(msg) + 1); 
    close(fd);
}

void broadcast_all(char *msg) {
    for (int i = 1; i <= 3; i++) {
        send_to_player(i, msg);
    }
}

int main() {
    int fd_server;
    char buffer[100];
    int player_id, guess;
    int target_number;
    int current_turn = 1; // Start with Player 1

    srand(time(NULL));
    target_number = (rand() % 100) + 1;

    // Create main pipe
    mkfifo(SERVER_PIPE, 0666);

    printf("--- TURN-BASED SERVER STARTED ---\n");
    printf("Target: %d\n", target_number);
    printf("Wait for all 3 players to launch, then press ENTER to start game...");
    getchar(); // Manual start so you have time to open player windows

    // Start the game by telling Player 1 to go
    printf("Game Started. Signaling Player 1.\n");
    send_to_player(1, "GO");

    fd_server = open(SERVER_PIPE, O_RDONLY);

    while (1) {
        // Wait for a guess
        int bytes = read(fd_server, buffer, sizeof(buffer));
        
        if (bytes > 0) {
            sscanf(buffer, "%d %d", &player_id, &guess);
            printf("Player %d guessed: %d\n", player_id, guess);

            // Security Check: Is it actually their turn?
            if (player_id != current_turn) {
                printf("Ignored out of turn guess.\n");
                continue;
            }

            if (guess == target_number) {
                // --- WINNER LOGIC ---
                char win_msg[100];
                sprintf(win_msg, "*** PLAYER %d WON! (Ans: %d) NEW GAME STARTING ***", player_id, target_number);
                
                // 1. Alert EVERYONE
                broadcast_all(win_msg);

                // 2. Reset Game
                target_number = (rand() % 100) + 1;
                printf("New Game. New Target: %d\n", target_number);
                current_turn = 1; 
                
                // 3. Let Player 1 start new game
                sleep(1); // Small delay so messages don't overlap
                send_to_player(1, "GO");

            } else {
                // --- WRONG GUESS LOGIC ---
                char hint_msg[100];
                if (guess < target_number) strcpy(hint_msg, "Too Low! Wait for turn...");
                else strcpy(hint_msg, "Too High! Wait for turn...");
                
                // 1. Tell current player they were wrong
                send_to_player(current_turn, hint_msg);

                // 2. Pass turn to next player (1 -> 2 -> 3 -> 1)
                current_turn++;
                if (current_turn > 3) current_turn = 1;

                // 3. Signal next player
                printf("Signaling Player %d\n", current_turn);
                send_to_player(current_turn, "GO");
            }
        }
    }
    return 0;
}