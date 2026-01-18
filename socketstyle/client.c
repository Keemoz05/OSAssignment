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
#include <arpa/inet.h>
#include <sys/select.h>
#include <ctype.h>

#define PORT 8080
#define SIGNAL_GAME_START 1
#define SIGNAL_YOUR_TURN  2
#define SIGNAL_GAME_OVER  3
#define TURN_TIMEOUT 15 // Seconds allowed per turn

int main(int argc, char const *argv[]) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};
    char name[20];
    const char* server_ip = "127.0.0.1";
    // ==================================================================================
    //   SECTION 1: CONNECTION SETUP
    // ==================================================================================

     // MODE CHECK: Did the server launch us with an IP?
    if (argc > 1) server_ip = argv[1];

    // Single attempt for auto-mode
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed. Is the server running?\n");
        return -1;
    }

    // ==================================================================================
    //   SECTION 2: HANDSHAKE
    // ==================================================================================
    printf("\n>>> CONNECTED TO SERVER <<<\n");
    printf("Enter your name: ");
    scanf("%19s", name);

    /* FIX: clear stdin so first fgets() does not instantly read newline */
    int c;
    while ((c = getchar()) != '\n' && c != EOF);

    send(sock, name, strlen(name) + 1, 0);
    printf("Waiting for other players...\n");

    // 2. Wait for Game Start
    int signal;
    if (recv(sock, &signal, sizeof(int), 0) <= 0) {
        printf("\n[Disconnected] Server closed connection.\n");
        close(sock);
        return 0;
    }

    if (signal == SIGNAL_GAME_START) {
        printf("\n=============================\n");
        printf("   GAME STARTED! GOOD LUCK!  \n");
        printf("=============================\n");
    }

    // 3. Main Game Loop
    while (1) {
        // Wait for server signal (Turn or Game Over)
        if (recv(sock, &signal, sizeof(int), 0) <= 0) {
            printf("\n====================================\n");
            printf("   GAME OVER! Server ended the game\n");
            printf("====================================\n");
            break;
        }

        if (signal == SIGNAL_YOUR_TURN) {
            int my_score;
            if (recv(sock, &my_score, sizeof(int), 0) <= 0) break;
            
            printf("\n===============================");
            printf("\n--- IT IS YOUR TURN ---\n");
            printf("\n--- CURRENT SCORE: %d ---", my_score);
            printf("\n===============================\n");
            printf("Enter 5-letter guess (You have %d seconds): ", TURN_TIMEOUT);
            fflush(stdout);

            // --- TIMEOUT LOGIC USING select() ---
            fd_set readfds;
            struct timeval tv;
            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);

            tv.tv_sec = TURN_TIMEOUT;
            tv.tv_usec = 0;

            int retval = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);

            if (retval == -1) {
                perror("select()");
                break;
            } else if (retval) {
                char guess[20] = {0};
                fgets(guess, 20, stdin);
                guess[strcspn(guess, "\n")] = 0;

                if (strlen(guess) == 0) {
                    send(sock, "__TIMEOUT__", 12, 0);
                } else {
                    send(sock, guess, strlen(guess) + 1, 0);
                }
            } else {
                printf("\n[TIMEOUT] You took too long!\n");
                send(sock, "__TIMEOUT__", 12, 0);
            }

            // Receive feedback (Result or Skip Message)
            char result[1024] = {0};
            if (recv(sock, result, 1024, 0) <= 0) {
                printf("\n====================================\n");
                printf("   GAME OVER! Server ended the game\n");
                printf("====================================\n");
                break;
            }

            printf("Server Feedback: %s\n", result);

            if (strcmp(result, "GGGGG") == 0) {
                printf("CORRECT! Resetting board...\n");
            }

        } else if (signal == SIGNAL_GAME_OVER) {
            char winner[20] = {0};
            recv(sock, winner, 20, 0);
            printf("\n====================================\n");
            printf("   GAME OVER! Winner: %s\n", winner);
            printf("====================================\n");
            break;
        }
    }

    close(sock);
    return 0;
}
