#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_PLAYERS 10

int main() {
    int player_amount;

    printf("Enter number of players (max %d): ", MAX_PLAYERS);
    scanf("%d", &player_amount);

    // open server terminal
    if (fork() == 0) {
        execlp("xterm", "xterm", "-T", "Server",
               "-e", "./server", NULL);
        exit(1);
    }

    sleep(1); // wait a bit for server to start

    // open player terminals
    for (int i = 0; i < player_amount; i++) {
        if (fork() == 0) {
            char title[20];
            sprintf(title, "Player %d", i + 1);
            execlp("xterm", "xterm", "-T", title,
                   "-e", "./player", NULL);
            exit(1);
        }
    }

    printf("Launcher running...\n");
    while (1); // keep launcher alive
}
