/*
 * ======================================================================================
 * CLIENT APPLICATION
 * ======================================================================================
 * * LOGIC:
 * 1. Connection: Supports Auto-Connect (args) or Manual Input (loop).
 * 2. Game Loop: dumb terminal. Waits for signals from Server.
 * - SIGNAL_GAME_START: Print header.
 * - SIGNAL_YOUR_TURN: Input loop (validation) -> Send -> Wait for Result.
 *
 * COMPILE: gcc client.c -o client
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/select.h> // Added for v2 timeout functionality

#define PORT 8080
#define SIGNAL_GAME_START 1
#define SIGNAL_YOUR_TURN  2
#define SIGNAL_GAME_OVER  3
#define SIGNAL_WAIT_RESTART 4
#define TURN_TIMEOUT 15 // Seconds allowed per turn (from v2)

int main(int argc, char const *argv[]) {
    int sock = 0;
    struct sockaddr_in server_address;
    char name[20], guess[20], result[20];
    char ip_input[50];
    const char *server_ip; 

    // ==================================================================================
    //    SECTION 1: CONNECTION SETUP
    // ==================================================================================

    // MODE CHECK: Did the server launch us with an IP?
    if (argc > 1) {
        server_ip = argv[1];
        printf("Auto-connecting to: %s\n", server_ip);

        // Single attempt for auto-mode
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { perror("Socket error"); return -1; }
        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(PORT);
        if (inet_pton(AF_INET, server_ip, &server_address.sin_addr) <= 0) { printf("Invalid IP\n"); return -1; }
        if (connect(sock, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) { printf("Connection Failed\n"); return -1; }
    
    } else {
        // MANUAL MODE: Loop until valid connection
        while (1) {
            // Sockets must be recreated per attempt
            if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { perror("Socket error"); exit(1); }
            server_address.sin_family = AF_INET;
            server_address.sin_port = htons(PORT);

            printf("Enter Server IP (Press ENTER for Localhost): ");
            fgets(ip_input, 50, stdin);
            ip_input[strcspn(ip_input, "\n")] = 0;

            if (strlen(ip_input) == 0) server_ip = "127.0.0.1";
            else server_ip = ip_input;

            if (inet_pton(AF_INET, server_ip, &server_address.sin_addr) <= 0) {
                printf(">> Invalid IP Address. Try again.\n");
                close(sock); continue;
            }

            printf("Connecting to %s...\n", server_ip);
            if (connect(sock, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
                printf(">> Connection Failed. Retrying...\n");
                close(sock); continue;
            }
            break; // Connection successful
        }
    }

    // ==================================================================================
    //    SECTION 2: HANDSHAKE
    // ==================================================================================
    printf("\n>>> CONNECTED TO SERVER <<<\n");
    printf("Enter your name: ");
    scanf("%19s", name);

    /* FIX: clear stdin so first fgets() does not instantly read newline (from v2) */
    int c;
    while ((c = getchar()) != '\n' && c != EOF);

    send(sock, name, strlen(name) + 1, 0);
    printf("Waiting for other players...\n");


    // ==================================================================================
    //    SECTION 3: GAME EVENT LOOP
    // ==================================================================================
    while (1) {
        int signal_received;
        int bytes = recv(sock, &signal_received, sizeof(int), 0);
        
        if (bytes <= 0) { printf("\nServer disconnected.\n"); break; }//idk how to handle this :(


        
        // --- CASE: WAIT FOR RESTART DECISION ---
        else if (signal_received == SIGNAL_WAIT_RESTART) {
            char winner[20];
            recv(sock, winner, 20, 0); // Receive the winner's name from server
            
            printf("\n============================================\n");
            printf("   GRAND CHAMPION: %s\n", winner);
            printf("============================================\n");
            printf("\n>>> Awaiting server input... <<<\n");
            
            // Wait for next signal from server (GAME_START for restart, GAME_OVER for shutdown)
            // No input is allowed here - client just waits
            continue;
        }

        // --- CASE 1: GAME START ---
        if (signal_received == SIGNAL_GAME_START) {
            printf("\n>>> GAME STARTED! First to 3 wins takes the match!<<<\n");
        } 
        
        // --- CASE 2: MY TURN ---
        else if (signal_received == SIGNAL_YOUR_TURN) {
            memset(guess, 0, sizeof(guess));
            int my_score;
            int fiveChar = 0;
            recv(sock, &my_score, sizeof(int), 0); //Receive score from server
            printf("\n===============================");
            printf("\n--- IT IS YOUR TURN ---\n");
            printf("\n--- CURRENT SCORE: %d ---", my_score); // Display it!
            printf("\n===============================\n");
            
            // Replaced v1 Validation Loop with v2 Timeout Logic
        
        do{
            printf("Enter 5-letter guess (You have %d seconds): ", TURN_TIMEOUT);
            fflush(stdout);

            // --- TIMEOUT LOGIC USING select() ---
            fd_set readfds;
            struct timeval tv;
            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);

            tv.tv_sec = TURN_TIMEOUT; //time out in seconds
            tv.tv_usec = 0; //we dont need microsecond time values

            int retval = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv); 
            //STDIN_FILENO + 1 = STDOUT_FILENO, FD for the standard output stream, usually the screen
        

            if (retval == -1) {
                perror("select()");
                break;
            } else if (retval) {

                memset(guess, 0, sizeof(guess));
                fgets(guess, 20, stdin);
                guess[strcspn(guess, "\n")] = 0;

                 if (strlen(guess) != 5) {
                    printf("Invalid input. Must be exactly 5 letters.\n");
                    } else {
                            send(sock, guess, strlen(guess) + 1, 0);
                     }   

            } else if(strlen(guess) == 0) {
                // Timeout occurred
                printf("\n[TIMEOUT] You took too long!\n");
                send(sock, "__TIMEOUT__", 12, 0);
                break;
               
            }
            
            } while (strlen(guess) != 5);
            
            // 2. Wait for Evaluation (G/Y/X string)
            memset(result, 0, sizeof(result));
            recv(sock, result, sizeof(result), 0);
            printf("Feedback: %s\n", result);
            
            if (strcmp(result, "GGGGG") == 0) printf("*** YOU WON THIS ROUND! +1 Score ***\n");
            printf("Waiting for next turn...\n");
        }
    }
    close(sock);
    return 0;
}
