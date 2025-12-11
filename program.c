#include <stdio.h> 
#include <stdlib.h> 
#include <time.h> 
#include <unistd.h> 
#include <sys/wait.h> 
#include <pthread.h> // REQUIRED: For threading

// This variable controls if the timer should keep running
// 1 = run, 0 = stop
int game_active = 1; 
int seconds = 0;
// This is the function the Thread will run
void* timer_thread(void* arg) {
    while (game_active == 1) {
        sleep(1); // Wait 1 second
        seconds++;
        
        // This makes sure the prompt shows up again after the timer prints
        // (Visual cleanup only)
        // printf("Guess again: "); 
        // fflush(stdout);
    }
    return NULL;
}

int main(){
    int fd[2]; 
    if (pipe(fd) == -1) { return 1; }

    pid_t pid = fork(); 

    if (pid < 0) { return 1; }

    // ---------------------------------------------------------
    // CHILD PROCESS
    // ---------------------------------------------------------
    if (pid == 0) { 
        int targetnumber;
        close(fd[1]); 
        read(fd[0], &targetnumber, sizeof(int));
        close(fd[0]);

        printf("[Child] I received the secret number. Game on!\n");
        
        int userguess;
        int correct = 0;

        while (!correct) {
            printf("[Child] Guess the number!: ");
            scanf("%d", &userguess);

            if((targetnumber - userguess) > 0){
                printf("[Child] Higher!\n");
            }
            else if((targetnumber - userguess) < 0){
                printf("[Child] Lower!\n");
            }
            else{
                printf("[Child] You got it! The number was %d\n", targetnumber);
                correct = 1;
            }
        }
        exit(0); 
    }
    
    // ---------------------------------------------------------
    // PARENT PROCESS (Main Thread + Timer Thread)
    // ---------------------------------------------------------
    else { 
        close(fd[0]); 
        srand(time(NULL));
        int secret = rand() % 100 + 1;
        // int secret = 67; 
        
        printf("[Parent] Secret number %d sent to child.\n", secret);
        write(fd[1], &secret, sizeof(int));
        close(fd[1]); 

        // 1. Create the Thread
        pthread_t tid;
        pthread_create(&tid, NULL, timer_thread, NULL);

        // 2. Wait for Child to finish
        // The Parent pauses here, BUT the thread keeps running!
        wait(NULL); 
        
        // 3. Stop the Thread
        // Once wait() finishes (meaning child won), we turn off the flag
        game_active = 0; 
        
        // Wait for the thread to see the flag and exit cleanly
        pthread_join(tid, NULL); 
        printf("\n   [Timer] Time Taken: %d seconds\n", seconds);
        printf("[Parent] Game over. Timer stopped.\n");
    }

    return 0;
}