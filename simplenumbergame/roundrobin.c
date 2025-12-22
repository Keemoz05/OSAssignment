#include <stdio.h> 
#include <stdlib.h> 
#include <time.h> 
#include <unistd.h> 
#include <sys/wait.h> 
#include <pthread.h> 
#include <fcntl.h>      
#include <semaphore.h>  
#include <errno.h>
#include <string.h>
#include <signal.h>
#define NUM_PLAYERS 3

const char* SEM_NAMES[] = { "/sem_p1", "/sem_p2", "/sem_p3" };

// --- REQUIREMENT: SAFE LOGGING & THREAD SYNCHRONIZATION ---
// We use a pipe for the Logger: Everyone writes to log_pipe[1], Thread reads log_pipe[0]
int log_pipe[2]; 
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex to protect file writing

// Helper function to send log messages safely
void safe_log(char* message) {
    write(log_pipe[1], message, strlen(message));
}

// --- REQUIREMENT: MULTITHREADING (Logger) ---
// Reads from the pipe and prints to screen/file safely
void* logger_thread(void* arg) {
    char buffer[256];
    int nbytes;
    
    // Open a log file
    FILE *f = fopen("game_log.txt", "w");
    if (!f) return NULL;

    while (1) {
        // Read raw bytes from the logging pipe
        nbytes = read(log_pipe[0], buffer, 255);
        if (nbytes <= 0) break; // Pipe closed

        buffer[nbytes] = '\0'; // Null-terminate string

        // REQUIREMENT: SYNCHRONIZATION (Mutex)
        pthread_mutex_lock(&log_mutex);
        
        // Write to Console
        printf("%s", buffer);
        // Write to File
        fprintf(f, "%s", buffer);
        fflush(f); // Ensure it saves immediately
        
        pthread_mutex_unlock(&log_mutex);
    }
    fclose(f);
    return NULL;
}

// --- REQUIREMENT: MULTITHREADING (Internal Server Task) ---
int game_running = 1;
void* timer_thread(void* arg) {
    int seconds = 0;
    char msg[50];
    while (game_running) {
        sleep(5); // Log every 5 seconds to reduce clutter
        seconds += 5;
        
        sprintf(msg, "   [Server Stats] Game running for %d seconds...\n", seconds);
        safe_log(msg);
    }
    return NULL;
}

int main(){
    // 1. SETUP LOGGING PIPE
    if(pipe(log_pipe) == -1) { return 1; }

    // 2. START LOGGER THREAD
    pthread_t log_tid, timer_tid;
    pthread_create(&log_tid, NULL, logger_thread, NULL);

    int pipes[NUM_PLAYERS][2]; 

    // Cleanup old semaphores
    for(int i=0; i < NUM_PLAYERS; i++) sem_unlink(SEM_NAMES[i]);

    // Parent Setup
    for(int i=0; i < NUM_PLAYERS; i++){
        pipe(pipes[i]);
        sem_t *sem = sem_open(SEM_NAMES[i], O_CREAT, 0644, 0);
        sem_close(sem);
    }

    safe_log("[Parent] Server Started. Creating players...\n");

    // 3. REQUIREMENT: MULTIPROCESSING (Fork)
    for(int i=0; i < NUM_PLAYERS; i++) {
        pid_t pid = fork(); 

        if (pid == 0) { 
            // --- CHILD PROCESS ---
            // Close the Read end of log pipe (Child only writes logs)
            close(log_pipe[0]); 

            int myID = i; 
            int nextID = (i + 1) % NUM_PLAYERS;
            
            // REQUIREMENT: SYNCHRONIZATION (Semaphores)
            sem_t *mySem = sem_open(SEM_NAMES[myID], 0);
            sem_t *nextSem = sem_open(SEM_NAMES[nextID], 0);

            // Get Secret via IPC (Pipe)
            int targetnumber;
            close(pipes[i][1]); 
            read(pipes[i][0], &targetnumber, sizeof(int));
            close(pipes[i][0]);
            
            char logmsg[100];
            sprintf(logmsg, "[Player %d] Ready. Waiting for turn.\n", myID+1);
            safe_log(logmsg);
            
            int userguess;
            
            while (1) {
                // REQUIREMENT: ROUND ROBIN (Wait for turn)
                sem_wait(mySem); 

                // Prompt user (Direct printf for input prompt is okay)
                printf("\n[Player %d] YOUR TURN. Guess: ", myID+1);
                fflush(stdout);

                scanf("%d", &userguess);

                // Send result to Logger
                if(targetnumber > userguess) {
                    sprintf(logmsg, "[Player %d] Guessed %d -> Higher!\n", myID+1, userguess);
                    safe_log(logmsg);
                }
                else if(targetnumber < userguess) {
                    sprintf(logmsg, "[Player %d] Guessed %d -> Lower!\n", myID+1, userguess);
                    safe_log(logmsg);
                }
                else {
                    sprintf(logmsg, "[Player %d] WINNER! Found %d.\n", myID+1, targetnumber);
                    safe_log(logmsg);
                    
                    // Cleanup and Exit
                    sem_close(mySem);
                    sem_close(nextSem);
                    exit(0); 
                }

                // REQUIREMENT: ROUND ROBIN (Pass turn)
                sprintf(logmsg, "[Player %d] Passing turn to Player %d.\n", myID+1, nextID+1);
                safe_log(logmsg);
                sem_post(nextSem); 
            }
        }
    }

    // --- PARENT SERVER LOGIC ---
    // Start Timer Thread
    pthread_create(&timer_tid, NULL, timer_thread, NULL);

    srand(time(NULL));
    int secret = rand() % 100 + 1;
    char msg[100];
    sprintf(msg, "[Parent] Secret number is %d.\n", secret);
    safe_log(msg);

    // REQUIREMENT: IPC (Pipes for Secret Number)
    for(int i=0; i < NUM_PLAYERS; i++){
        write(pipes[i][1], &secret, sizeof(int));
        close(pipes[i][1]); 
    }

    sleep(1);
    safe_log("[Parent] Kicking off Round Robin!\n");
    sem_t *sem1 = sem_open(SEM_NAMES[0], 0);
    sem_post(sem1);
    sem_close(sem1);

    // Wait for winner
    wait(NULL);

    // STOP SERVER TASKS
    game_running = 0; // Stop timer loop
    pthread_cancel(timer_tid); // Kill timer immediately
    pthread_join(timer_tid, NULL);

    safe_log("[Parent] Game Over. Killing processes...\n");
    kill(0, SIGTERM);

    // Close Logging
    close(log_pipe[1]); // Close write end so logger thread can finish
    pthread_join(log_tid, NULL);

    // Final Semaphore Cleanup
    for(int i=0; i < NUM_PLAYERS; i++) sem_unlink(SEM_NAMES[i]);
    
    return 0;
}