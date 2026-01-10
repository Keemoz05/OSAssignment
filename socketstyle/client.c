#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 8080
#define SIGNAL_GAME_START 1
#define SIGNAL_YOUR_TURN  2



int main() {
    int sock = 0;
    struct sockaddr_in server_address;
    char name[20], guess[10];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    printf("Enter your name: ");
    scanf("%s", name);
    send(sock, name, strlen(name) + 1, 0);



    while (1) {
        int signal_received;
        int n = recv(sock, &signal_received, sizeof(int), 0);

        if (n <= 0) {
            printf("Connection lost or Server closed. Exiting...\n");
            break;
        }

        if (signal_received == SIGNAL_GAME_START) {
            printf("Game Started! Wait for your turn...\n");
        } 
        else if (signal_received == SIGNAL_YOUR_TURN) {
            printf("\n--- IT IS YOUR TURN ---\nEnter guess: ");
            scanf("%s", guess);
            send(sock, guess, strlen(guess) + 1, 0);
        }
    }
    close(sock);
    return 0;
}