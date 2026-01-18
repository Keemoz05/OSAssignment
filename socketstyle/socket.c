/*
 * ======================================================================================
 * MULTIPROCESS & MULTITHREADED WORD GUESS SERVER
 * ======================================================================================
 * * ARCHITECTURE SUMMARY:
 * 1. Shared Memory: Holds game state (turn, scores, word) accessible by all processes.
 * 2. Main Process: Listens for connections (accept) and forks child processes.
 * 3. Child Processes: One per player. Handles direct communication (recv/send).
 * 4. Scheduler Thread: Runs in Main Process. Manages turn rotation and game flow.
 *
 * COMPILE: gcc server.c -o server -pthread
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <signal.h>
#include <sys/mman.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h> 
#include <ctype.h>
#include <time.h>
#include <sys/wait.h> 

// ======================================================================================
//   SECTION 1: CONSTANTS & SHARED MEMORY DEFINITIONS
// ======================================================================================

#define PORT 8080
#define MAX_PLAYERS 4
#define MAX_MISSED_TURNS 3 // 3 Strikes Rule
#define WORD_LENGTH 5
#define SIGNAL_GAME_START 1
#define SIGNAL_YOUR_TURN  2
#define SIGNAL_GAME_OVER  3
#define MAX_LOG_MSG 128
#define LOG_QUEUE_SIZE 100

// Database of words for the game
const char word_database[][6] = {
    "APPLE", "BEACH", "BRAIN", "BREAD", "BRUSH",
    "CHAIR", "CHEST", "CHORD", "CLICK", "CLOCK",
    "CLOUD", "DANCE", "DIARY", "DRINK", "DRIVE",
    "EARTH", "FEAST", "FIELD", "FRUIT", "GLASS"
};
#define WORD_COUNT 20

// Data specific to a single player
typedef struct {
    int id;             // Internal ID (0 to 3)
    int is_active;      // 1 = Online, 0 = Offline
    char name[20];
    pid_t pid;          // Process tracking
    int missed_turns;   // Counts how many turns a player has missed a turn
    int score;
} PlayerData;

typedef struct {
    char tag[16];
    char msg[MAX_LOG_MSG];
    char file[32];
    int pid;
} LogEntry;

// THE SHARED MEMORY OBJECT
// Everything in here is visible to Parent and all Children
typedef struct {
    PlayerData players[MAX_PLAYERS];
    LogEntry log_queue[LOG_QUEUE_SIZE]; 
    int player_count;    
    char target_word[6]; 

    // Persistent Scoring
    struct {
        char name[20];
        int wins;
    } scoreboard[100]; 
    int total_recorded_players;
    
      // Game State Flags
    int current_turn_index; // 0 to 3, indicates who plays now
    int game_running;       // 1 = Game On, 0 = Game Over
    int game_started;       // 1 = All players connected, start allowed
    int turn_completed;     // Handshake flag to prevent double-turns
    int log_head;           // logging queue head
    int log_tail;           // logging queue tail
    
    // POSIX Synchronization Primitives (Must be process-shared)
    pthread_mutex_t mutex;           // Main lock for reading/writing shared memory
    pthread_cond_t cond_game_start;  // "Barrier": wait here until lobby is full
    pthread_cond_t cond_turn_start;  // Signal: "Wake up and check if it's your turn"
    pthread_cond_t cond_turn_end;    // Signal: "I finished my turn, Scheduler can proceed"
    pthread_mutex_t log_mutex;   // mutex for logging 
    pthread_cond_t log_cond;    // condition variable for logging
} GameState;

// Global pointer to shared memory
GameState *shm;

// Function Prototypes
void setup_shared_memory();
void pick_new_word();
void evaluate_guess(char guess[], const char target[], char result[]);
void *scheduler_thread_func(void *arg);
void handle_client_session(int socket, int player_id);
void print_local_ip();
void log_event(const char* tag , const char* message , const char* player_logfile);



//Safely removes a player. 
 
void client_cleanup(int player_id, char* reason){
    pthread_mutex_lock(&shm->mutex);
    
    if (shm->players[player_id].is_active){
        printf("[System] Disconnecting Player %d: %s\n", player_id + 1, reason);
        
        shm->players[player_id].is_active = 0;
        shm->players[player_id].pid = 0;

        //release turn
        if (shm->current_turn_index == player_id) {
            shm->turn_completed = 1;
            pthread_cond_broadcast(&shm->cond_turn_end);
        }
    }
    
    pthread_mutex_unlock(&shm->mutex);
    exit(0); // Terminates process -> Triggers SIGCHLD in Parent
}

// sig handler for zombo reap
void reaper(int signal) { 
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

// --- Logging Thread ---
void *logger_thread_func(void *arg){
    while (shm->game_running){
        pthread_mutex_lock(&shm->log_mutex);
        while (shm->log_head == shm->log_tail && shm->game_running){
            pthread_cond_wait(&shm->log_cond , &shm->log_mutex);
        }
        if (!shm->game_running){
             pthread_mutex_unlock(&shm->log_mutex);
             break;
        }

        LogEntry *entry = &shm->log_queue[shm->log_tail];
        shm->log_tail = (shm->log_tail + 1) % LOG_QUEUE_SIZE;
        pthread_mutex_unlock(&shm->log_mutex);

        time_t now = time(NULL);
        struct tm *t = localtime(&now);

        // Global Log
        FILE *f_main = fopen("game.log", "a");
        if (f_main) {
            fprintf(f_main, "[%02d:%02d:%02d] [PID:%d] [%s] %s\n", 
                    t->tm_hour, t->tm_min, t->tm_sec, entry->pid, entry->tag, entry->msg);
            fclose(f_main);
        }

        // Player Specific Log
        if (strlen(entry->file) > 0){
            FILE *f = fopen(entry->file, "a");
            if (f){
              fprintf(f , "[%02d:%02d:%02d] [PID:%d] [%s] %s\n" , t->tm_hour , t->tm_min , t->tm_sec , entry->pid , entry->tag , entry->msg);
              fclose(f);
            }
        }
    }
    return NULL;
}


// make log event on each pthread
void log_event(const char* tag , const char* message , const char* player_logfile){
     pthread_mutex_lock(&shm->log_mutex);
     LogEntry *entry = &shm->log_queue[shm->log_head];
     strncpy(entry->tag , tag , 15);
     entry->tag[15] = '\0';
     strncpy(entry->msg , message , MAX_LOG_MSG - 1);
     entry->msg[MAX_LOG_MSG - 1] =  '\0';

     if (player_logfile){
        strncpy(entry->file , player_logfile , 31);
        entry->file[31] = '\0';
     } else {
        entry->file[0] = '\0';
     }
     entry->pid = getpid();
     shm->log_head = (shm->log_head + 1) % LOG_QUEUE_SIZE;

    pthread_cond_signal(&shm->log_cond);
    pthread_mutex_unlock(&shm->log_mutex);
}

// --- Score Management ---
void load_scores_from_file() {
    FILE *f = fopen("scores.txt", "r");
    shm->total_recorded_players = 0;
    if (!f) return;

    while (fscanf(f, "%19s %d", shm->scoreboard[shm->total_recorded_players].name, 
                  &shm->scoreboard[shm->total_recorded_players].wins) == 2) {
        shm->total_recorded_players++;
        if (shm->total_recorded_players >= 100) break;
    }
    fclose(f);
}

void save_scores_to_file() {
     // Note: Mutex should be held when calling this if accessed via threads
    FILE *f = fopen("scores.txt", "w");
    if (!f) return;
    for (int i = 0; i < shm->total_recorded_players; i++) {
        fprintf(f, "%s %d\n", shm->scoreboard[i].name, shm->scoreboard[i].wins);
    }
    fclose(f);
}

//ensures that even if we force-close the server with Ctrl+C, the scores.txt file is finalized
void handle_shutdown(int sig) {
    printf("\n[System] Shutdown signal received. Saving scores...\n");
    if (shm != NULL) {
        pthread_mutex_lock(&shm->mutex);
        save_scores_to_file();
        pthread_mutex_unlock(&shm->mutex);
        munmap(shm, sizeof(GameState));
    }
    printf("[System] Scores saved. Goodbye!\n");
    exit(0);
}

// ======================================================================================
//   SECTION 2: MAIN ENTRY POINT
// ======================================================================================

int main() {
    srand(time(NULL)); 
    signal(SIGPIPE, SIG_IGN); 
    signal(SIGINT, handle_shutdown); 

    struct sigaction sa;
    sa.sa_handler = reaper; 
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    int server_fd, mode;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    //Initialize Shared Memory (mmap)
    setup_shared_memory();
    load_scores_from_file(); 

    pthread_t logger_thread_id;
    pthread_create(&logger_thread_id , NULL , logger_thread_func , NULL);


    //User Configuration
    printf("--- WORD GUESS SERVER ---\n");
    printf("1. Local Mode (Auto-launch terminals)\n");
    printf("2. Network Mode (Wait for remote connections)\n");
    printf("Select: ");
    if (scanf("%d", &mode) != 1) return 1;



    // ------------ logging for server start ------------- //
    log_event("SYSTEM" , "Server started" , NULL);


    // ------------ logging for players who joined the game ------------- //
    printf("Enter number of players (Max %d): ", MAX_PLAYERS);
    if (scanf("%d", &shm->player_count) != 1) return 1;

    char log_msg[64];
    sprintf(log_msg, "Configured for %d players.", shm->player_count);
    log_event("SYSTEM" , log_msg , NULL);
   
    // Start the Referee (Scheduler) Thread
    // This thread runs in the background of the Parent Process
    pthread_t tid;
    pthread_create(&tid, NULL, scheduler_thread_func, NULL);

    // Socket Boilerplate
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed"); exit(1);
    }
    listen(server_fd, MAX_PLAYERS);

    // Handling Launch Modes
    if (mode == 1) {
        printf("[System] Launching %d local client terminals...\n", shm->player_count);
        for (int i = 0; i < shm->player_count; i++) {
            if (fork() == 0) {
                char title[20];
                sprintf(title, "Player %d", i + 1);
                execlp("xterm", "xterm", "-T", title, "-e", "./client", "127.0.0.1", NULL);
                exit(0);
            }
        }
    } else {
        printf("\n[System] Network Mode Enabled.\n");
        print_local_ip();
        printf("Ask %d players to connect to the IP above.\n", shm->player_count);
    }

    // Accept Loop
    int connected_count = 0;
    while (connected_count < shm->player_count) {
        printf("[System] Waiting for players: %d/%d connected...\n", connected_count, shm->player_count);
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);

        if (new_socket >= 0 ){
            char log_conn_msg[64];
            sprintf(log_conn_msg , "Player connected from %s:%d" , inet_ntoa(address.sin_addr) , ntohs(address.sin_port));
            log_event("SYSTEM" , log_conn_msg , NULL);
        }

        if (new_socket < 0) continue;

        if (fork() == 0) {
            // --- CHILD PROCESS CODE ---
            close(server_fd); // Child doesn't need the listener
            handle_client_session(new_socket, connected_count);
            exit(0); // Kill child when session ends, reaping will occur
        } else {
            // --- PARENT PROCESS CODE ---
            close(new_socket); // Parent doesn't need the specific client socket
            connected_count++;
        }
    }
    
    printf("[System] All players connected. Main process waiting for Scheduler.\n");

    // --------------------------- logging for game start ------------------------------- //

    char log_start_msg[64];
    sprintf(log_start_msg , "All %d players connected. Game starting." , shm->player_count);
    log_event("SYSTEM" , log_start_msg , NULL);

    pthread_join(tid, NULL); 
    
    // Cleanup
    pthread_mutex_destroy(&shm->mutex);
    pthread_cond_destroy(&shm->cond_game_start);
    pthread_cond_destroy(&shm->cond_turn_start);
    pthread_cond_destroy(&shm->cond_turn_end);
    munmap(shm, sizeof(GameState));
    
    return 0;
}


// ======================================================================================
//   SECTION 3: GAME LOGIC & UTILS
// ======================================================================================

void pick_new_word() {
    int idx = rand() % WORD_COUNT;
    strcpy(shm->target_word, word_database[idx]);
    printf("[Game] NEW WORD SELECTED: %s\n", shm->target_word);
}

void evaluate_guess(char guess[], const char target[], char result[]) {
    // Standard Wordle Logic: G=Green, Y=Yellow, X=Gray
    int letter_budget[26] = {0}; 
    int i; 

    // Init result
    for (i = 0; i < WORD_LENGTH; i++) result[i] = 'X';
    result[WORD_LENGTH] = '\0'; 

    // Build frequency budget
    for (i = 0; i < WORD_LENGTH; i++) {
        letter_budget[toupper(target[i]) - 'A']++;
    }

     // Pass 1: Green (Exact)
    for (i = 0; i < WORD_LENGTH; i++) {
        char g = toupper(guess[i]);
        char t = toupper(target[i]);
        if (g == t) {
            result[i] = 'G';
            letter_budget[g - 'A']--; 
        }
    }

    // Pass 2: Yellow (Wrong spot)
    for (i = 0; i < WORD_LENGTH; i++) {
        char g = toupper(guess[i]);
        if (result[i] == 'G') continue;
        if (letter_budget[g - 'A'] > 0) {
            result[i] = 'Y';
            letter_budget[g - 'A']--;
        }
    }
}

// Sets up mmap and Process-Shared Mutexes
void setup_shared_memory() {
    shm = mmap(NULL, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    
    // Attributes to allow mutexes to work across FORK
    pthread_mutexattr_t mattr; 
    pthread_condattr_t cattr;
    pthread_mutexattr_init(&mattr); 
    pthread_condattr_init(&cattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);


    // Init primitives
    pthread_mutex_init(&shm->mutex, &mattr);
    pthread_cond_init(&shm->cond_game_start, &cattr);
    pthread_cond_init(&shm->cond_turn_start, &cattr);
    pthread_cond_init(&shm->cond_turn_end, &cattr);
    pthread_mutex_init(&shm->log_mutex , &mattr);
    pthread_cond_init(&shm->log_cond , &cattr);

    // Defaults
    shm->current_turn_index = -1;
    shm->game_running = 1;
    shm->game_started = 0; 
    shm->turn_completed = 1; 
    shm->log_head = 0;
    shm->log_tail = 0;
    pick_new_word();
    for(int i=0; i<MAX_PLAYERS; i++) { 
        shm->players[i].is_active = 0; 
        shm->players[i].score = 0; 
        shm->players[i].missed_turns = 0;
        shm->players[i].pid = 0;
    }
}


// ======================================================================================
//   SECTION 4: THE SCHEDULER (REFEREE)
// ======================================================================================

void *scheduler_thread_func(void *arg) {
    // 1. WAIT PHASE: Pause until all players connect
    while(1) {
        sleep(1); 
        int ready = 0;
        pthread_mutex_lock(&shm->mutex);
        for(int i=0; i < shm->player_count; i++) if(shm->players[i].is_active) ready++;
        pthread_mutex_unlock(&shm->mutex);
        
        if(ready == shm->player_count) break;
    }

    // 2. START PHASE: Wake up all waiting child processes
    pthread_mutex_lock(&shm->mutex);
    shm->game_started = 1;
    pthread_cond_broadcast(&shm->cond_game_start); 
    pthread_mutex_unlock(&shm->mutex);
    
    int current_idx = 0;
    log_event("SCHEDULER" , "Thread started, waiting for players..." , NULL);

    // 3. GAME LOOP: Manage turns
    while (shm->game_running) {
        pthread_mutex_lock(&shm->mutex);
        
        //skipping inactive player
        int checked = 0;
        while (shm->players[current_idx].is_active == 0) {
            printf("[Scheduler] Skipping inactive Player %d\n", current_idx + 1);
            current_idx = (current_idx + 1) % shm->player_count; //% to keep the iterator within bounds of player count to avoid segmentation error
            checked++;
            
            if (checked > shm->player_count) {
                printf("[Scheduler] All players gone. Game Over.\n");
                shm->game_running = 0;
                pthread_mutex_unlock(&shm->mutex);
                return NULL;
            }
        }

        // B. Announce Turn 
        shm->current_turn_index = current_idx;
        shm->turn_completed = 0; 
        
        printf("[Scheduler] Turn: Player %d | Word: %s\n", current_idx + 1, shm->target_word);
        log_event("SCHEDULER" , "Broadcast turn start" , NULL);
        

        // C. Wait for turn completion
        // We sleep here until the child process signals 'cond_turn_end'

        pthread_cond_broadcast(&shm->cond_turn_start); // Wake up player

        // Wait for Turn End
        while (shm->turn_completed == 0 && shm->players[current_idx].is_active) {
            pthread_cond_wait(&shm->cond_turn_end, &shm->mutex);
        }

        log_event("SCHEDULER" , "Turn completed, advancing..." , NULL);
        current_idx = (current_idx + 1) % shm->player_count;
        pthread_mutex_unlock(&shm->mutex);
    }

    //A broadcast to signal everyone game is ending
    pthread_mutex_lock(&shm->mutex);
    int final_sig = SIGNAL_GAME_OVER;
    // Notify any waiting children (they might be stuck in a cond_wait) like waking em up
    pthread_cond_broadcast(&shm->cond_turn_start); 
    pthread_mutex_unlock(&shm->mutex);

    log_event("SCHEDULER", "Game finished.", NULL);
    return NULL;
}


// ======================================================================================
//   SECTION 5: THE CHILD PROCESS (PLAYER HANDLER + STRIKE LOGIC)
// ======================================================================================

void handle_client_session(int socket, int player_id) {
    char name[20];
    char player_log[30];
    
    // Basic handshake
    if (recv(socket, name, 20, 0) <= 0) { close(socket); return; }
    name[strcspn(name, "\n")] = 0;
    sprintf(player_log , "log_%s.txt" , name);

    // Register into Shared Memory
    pthread_mutex_lock(&shm->mutex);
    strcpy(shm->players[player_id].name, name);
    shm->players[player_id].id = player_id;
    shm->players[player_id].is_active = 1;
    shm->players[player_id].pid = getpid(); // Store PID for tracking
    shm->players[player_id].missed_turns = 0; // Reset strikes

    // Leaderboard logic
    int found = 0;
    for (int i = 0; i < shm->total_recorded_players; i++) {
        if (strcmp(shm->scoreboard[i].name, name) == 0) {
            found = 1;
            break;
        }
    }
    if (!found && shm->total_recorded_players < 100) {
        strcpy(shm->scoreboard[shm->total_recorded_players].name, name);
        shm->scoreboard[shm->total_recorded_players].wins = 0; 
        shm->total_recorded_players++;
        save_scores_to_file(); 
    }
    pthread_mutex_unlock(&shm->mutex);
    
   // BARRIER: Wait for Game Start Signal from Scheduler
    while (!shm->game_started) {
        pthread_mutex_lock(&shm->mutex);
        pthread_cond_wait(&shm->cond_game_start, &shm->mutex);
        pthread_mutex_unlock(&shm->mutex);
    }

    // Tell client "Game is starting"
    int sig = SIGNAL_GAME_START;
    send(socket, &sig, sizeof(int), 0);

    // MAIN SESSION LOOP
    while (1) {
        pthread_mutex_lock(&shm->mutex);
        
        // WAIT: Sleep until it is MY turn
        while ((shm->current_turn_index != player_id || shm->turn_completed == 1) && shm->game_running) {
            pthread_cond_wait(&shm->cond_turn_start, &shm->mutex);
        }
        
        // Check if game died while waiting
        if (!shm->game_running) { pthread_mutex_unlock(&shm->mutex); break; }
        log_event("CHILD" , "it is MY turn , sending data to client..." , player_log);
        pthread_mutex_unlock(&shm->mutex); 
        

        // --- MY TURN LOGIC ---
        sig = SIGNAL_YOUR_TURN;
         if (send(socket, &sig, sizeof(int), MSG_NOSIGNAL) <= 0) goto disconnect;


        //Send current score so client can display it
        int current_score = shm->players[player_id].score;
        send(socket, &current_score, sizeof(int), 0);

        char guess[20] = {0};
        char result[20] = {0};
        char log_msg[40];
        
        
        // Wait for user input
        int bytes = recv(socket, guess, sizeof(guess), 0);

        //client cleanup call
        if (bytes <= 0) client_cleanup(player_id, "Connection Lost");
        guess[strcspn(guess, "\n")] = 0;

        //timeout
        pthread_mutex_lock(&shm->mutex);

        // timeout flag
        if (strcmp(guess, "__TIMEOUT__") == 0) {
            shm->players[player_id].missed_turns++;
            int strikes = shm->players[player_id].missed_turns;

            printf("[Player %d] Timed Out (Strike %d/%d)\n", player_id+1, strikes, MAX_MISSED_TURNS);
            
            char log_msg[64];
            sprintf(log_msg, "Timed Out (Strike %d/%d)", strikes, MAX_MISSED_TURNS);
            log_event("TIMEOUT", log_msg, player_log);

            if (strikes >= MAX_MISSED_TURNS) {
                // 3 Strikes -> OUT
                pthread_mutex_unlock(&shm->mutex);
                // Note: client_cleanup handles notifying scheduler/Turn End
                client_cleanup(player_id, "Kicked: 3 Missed Turns");
            } else {
                // Just Skip this turn
                char msg[] = "Turn Skipped (Timeout)";
                send(socket, msg, strlen(msg)+1, 0);
                
                // Important: Signal turn end so Scheduler moves on
                shm->turn_completed = 1;
                pthread_cond_signal(&shm->cond_turn_end);
                pthread_mutex_unlock(&shm->mutex);
                continue; // Restart loop to wait for next turn
            }
        } 
        
        // Valid Input (Reset Strikes)
        shm->players[player_id].missed_turns = 0;

      
        evaluate_guess(guess, shm->target_word, result);
        
       // Win Check
        if (strcmp(result, "GGGGG") == 0) {
            pthread_mutex_lock(&shm->mutex);

            // 1. Update session score
            shm->players[player_id].score++;

            // 2. Update Persistent Scoreboard
            int found = 0;
            for (int i = 0; i < shm->total_recorded_players; i++) {
                if (strcmp(shm->scoreboard[i].name, name) == 0) {
                    shm->scoreboard[i].wins++;
                    found = 1;
                    break;
                }
            }

            if (!found && shm->total_recorded_players < 100) {
                strcpy(shm->scoreboard[shm->total_recorded_players].name, name);
                shm->scoreboard[shm->total_recorded_players].wins = 1;
                shm->total_recorded_players++;
            }

            save_scores_to_file();

            printf("!!! %s WON! Score: %d !!!\n", name, shm->players[player_id].score);
            sprintf(log_msg , "!!! %s WON! Score: %d !!!", name, shm->players[player_id].score);
            log_event("GAMEPLAY", log_msg, player_log);

            // --- MATCH WIN CONDITION ---
            if (shm->players[player_id].score >= 3) {
                printf("!!! MATCH OVER: %s is the Grand Champion !!!\n", name);
                sprintf(log_msg, "MATCH OVER: %s is the Grand Champion", name);
                log_event("GAMEPLAY", log_msg, player_log);

                shm->game_running = 0;

                // Wake everyone so they can exit cleanly
                shm->turn_completed = 1;
                pthread_cond_broadcast(&shm->cond_turn_start);
                pthread_cond_broadcast(&shm->cond_turn_end);
            } else {
                // Only reset word if match NOT over
                pick_new_word();
            }

            pthread_mutex_unlock(&shm->mutex);
        }

        // Finish Turn
   
        shm->turn_completed = 1;
        pthread_cond_signal(&shm->cond_turn_end);
        pthread_mutex_unlock(&shm->mutex);

        // Send Feedback to Client
        send(socket, result, strlen(result) + 1, 0);
        printf("[Player %s] Guessed: %s | Result: %s\n", name, guess, result);
        
        char log_game[64];
        sprintf(log_game, "Guessed: %s | Result: %s", guess, result);
        log_event("GAMEPLAY" , log_game , player_log);
    }
    client_cleanup(player_id, "Game End");


    disconnect:
    pthread_mutex_lock(&shm->mutex);
    shm->players[player_id].is_active = 0;
    shm->turn_completed = 1; // Release scheduler so it doesn't hang
    pthread_cond_signal(&shm->cond_turn_end);
    pthread_mutex_unlock(&shm->mutex);
    close(socket);
    exit(0);
}






// Utility to help user find their IP
void print_local_ip() {
    char host[256];
    struct hostent *host_entry;
    gethostname(host, sizeof(host));
    host_entry = gethostbyname(host);
    if (host_entry) {
        printf("Possible IP(s): ");
        for (int i = 0; host_entry->h_addr_list[i]; i++) {
             printf("%s ", inet_ntoa(*(struct in_addr*)host_entry->h_addr_list[i]));
        }
        printf("\n");
    }
}