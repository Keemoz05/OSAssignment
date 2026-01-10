#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#define PORT 8080
#define MAX_PLAYERS 4
#define SIGNAL_GAME_START 1
#define SIGNAL_YOUR_TURN  2

typedef struct {
    int playersocketid;
    char name[20];
    int is_active;
    int guess_count;
} Player;

int main() {
    signal(SIGPIPE, SIG_IGN); // Prevent crash on broken pipe

    int server_fd, player_count;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    Player player[MAX_PLAYERS];

    printf("Enter number of players (Max %d): ", MAX_PLAYERS);
    if (scanf("%d", &player_count) != 1) return 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    listen(server_fd, MAX_PLAYERS);

    // Launch terminals
    for (int i = 0; i < player_count; i++) {
        if (fork() == 0) {
            char title[20];
            sprintf(title, "Player %d", i + 1);
            execlp("xterm", "xterm", "-T", title, "-e", "./client", NULL);
            exit(0);
        }
    }

    // Connect players
    for (int i = 0; i < player_count; i++) {
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        player[i].playersocketid = new_socket;
        player[i].is_active = 1;
        player[i].guess_count = 0;

        int name_status = recv(new_socket, player[i].name, 20, 0);


        // check if name was received 
        if ( name_status <= 0){
    
            // handle disconnection before name is sent
            printf("Player %d disconnected before sending name.\n" , i + 1);
            player[i].is_active = 0;
            strcpy(player[i].name, "Unknown");
            close(new_socket);

        } else {
            // name received successfully
            player[i].is_active = 1;
            player[i].name[strcspn(player[i].name, "\n")] = '\0'; // Ensure null-termination
            printf("Player %d connected with name: %s\n", i + 1, player[i].name);

            

            int game_signal = SIGNAL_GAME_START;
            send(new_socket, &game_signal, sizeof(int), 0);

        }

        

    }

    int current = 0;

    
    while (1) {
        // FIXED: Only proceed if player IS active
        if (!player[current].is_active) {
            current = (current + 1) % player_count;
            
            // Safety: if all players are inactive, stop server
            int active_found = 0;

            for(int j=0; j<player_count; j++) {

                if(player[j].is_active) {

                    active_found=1;
                
                } else if(!active_found) { 

                    printf("All players gone. Ending.\n"); exit(0); 
                }

            }
            continue;
        }

        printf("\nIt's %s's turn!\n", player[current].name);

        int turn_signal = SIGNAL_YOUR_TURN;
        if (send(player[current].playersocketid, &turn_signal, sizeof(int), MSG_NOSIGNAL) <= 0) {
            printf("%s disconnected. Skipping.\n", player[current].name);
            player[current].is_active = 0; // FIXED: Set to 0
            current = (current + 1) % player_count;
            continue;
        }

        char guess[10] = {0};
        int valread = recv(player[current].playersocketid, guess, sizeof(guess), 0);

        if (valread <= 0) {
            printf("%s disconnected during turn.\n", player[current].name);
            player[current].is_active = 0; // FIXED: Set to 0
            current = (current + 1) % player_count;
            continue;
        }

        printf("%s guessed: %s\n", player[current].name, guess);
        player[current].guess_count++;
        current = (current + 1) % player_count;
    }
    return 0;
}