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

#define PORT 8080
#define SIGNAL_GAME_START 1
#define SIGNAL_YOUR_TURN  2

int main(int argc, char const *argv[]) {
    int sock = 0;
    struct sockaddr_in server_address;
    char name[20], guess[20], result[20];
    char ip_input[50];
    const char *server_ip; 

    // ==================================================================================
    //   SECTION 1: CONNECTION SETUP
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
    //   SECTION 2: HANDSHAKE
    // ==================================================================================
    printf("\n>>> CONNECTED TO SERVER <<<\n");
    printf("Enter your name: ");
    scanf("%19s", name);
    send(sock, name, strlen(name) + 1, 0);
    printf("Waiting for other players...\n");


    // ==================================================================================
    //   SECTION 3: GAME EVENT LOOP
    // ==================================================================================
    while (1) {
        int signal_received;
        int bytes = recv(sock, &signal_received, sizeof(int), 0);
        
        if (bytes <= 0) { printf("\nServer disconnected.\n"); break; }

        // --- CASE 1: GAME START ---
        if (signal_received == SIGNAL_GAME_START) {
            printf("\n>>> GAME STARTED! <<<\n");
        } 
        
        // --- CASE 2: MY TURN ---
        else if (signal_received == SIGNAL_YOUR_TURN) {
            printf("\n--- IT IS YOUR TURN ---\n");
            
            // Input Validation Loop
            while (1) {
                printf("Enter 5-letter guess: ");
                scanf("%19s", guess);
                
                if (strlen(guess) != 5) { printf(">> Error: Must be 5 letters.\n"); continue; }
                
                int valid = 1;
                for(int i=0; i<5; i++) if(!isalpha(guess[i])) valid = 0;
                if (!valid) { printf(">> Error: Letters only.\n"); continue; }
                
                break; // Valid input
            }
            
            // 1. Send Guess
            send(sock, guess, strlen(guess) + 1, 0);
            
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