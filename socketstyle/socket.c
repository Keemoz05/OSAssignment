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
//   SECTION 1: CONSTANT VARIABLES & SHARED MEMORY DEFINITIONS
// ======================================================================================

#define PORT 8080
#define MAX_PLAYERS 5
#define MAX_MISSED_TURNS 2 // 3 STRIKES FOR AFK RULE
#define WORD_LENGTH 5
#define SIGNAL_GAME_START 1
#define SIGNAL_YOUR_TURN  2
#define SIGNAL_GAME_OVER  3
#define SIGNAL_NEW_GAME   4  // Signal to restart game
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
    int id;             // Internal ID (0 to 4)
    int is_active;      // 1 = Online, 0 = Offline
    char name[20];
    pid_t pid;          // Process tracking
    int missed_turns;   // Counts how many turns a player has missed a turn
    int score;
} PlayerData;

typedef struct {
    char tag[16];        // Event type (SYSTEM, GAMEPLAY, SCHEDULER, CHILD)
    char msg[128];       // Log message
    char file[32];       // Optional player-specific log file
    int pid;             // Process ID for tracing
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
    int current_turn_index; // 0 to 4, indicates who plays now
    int game_running;       // 1 = Game On, 0 = Game Over
    int game_started;       // 1 = All players connected, start allowed
    int turn_completed;     // Handshake flag to prevent double-turns
    int new_game_pending;   // 1 = Server wants to restart, clients should wait
    int match_over;         // 1 = Someone won the match, waiting for host decision
    int match_winner_id;    // ID of the player who won the match (-1 if none)
    char match_winner_name[20]; // Name of match winner
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



//Safely disconnects a player
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
//WNOHANG collects the status of a dead process if there are any

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
    log_event("SYSTEM", log_msg, NULL);

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

    // ==================== OUTER GAME LOOP (Multi-Game Support) ====================
    int play_again = 1;
    while (play_again) {
        
        // Handling Launch Modes (only on first game for local mode)
        static int first_game = 1;
        if (mode == 1 && first_game) {
            printf("[System] Launching %d local client terminals...\n", shm->player_count);
            for (int i = 0; i < shm->player_count; i++) {
                if (fork() == 0) {
                    char title[20];
                    sprintf(title, "Player %d", i + 1);
                    execlp("xterm", "xterm", "-T", title, "-e", "./client", "127.0.0.1", NULL);
                    exit(0);
                }
            }
            first_game = 0;
        } else if (mode == 2 && first_game) {
            printf("\n[System] Network Mode Enabled.\n");
            print_local_ip();
            printf("Ask %d players to connect to the IP above.\n", shm->player_count);
            first_game = 0;
        } else if (!first_game) {
            printf("\n[System] Waiting for players to reconnect for new game...\n");
        }

        // Start the Referee (Scheduler) Thread for this game
        pthread_t tid;
        pthread_create(&tid, NULL, scheduler_thread_func, NULL);

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

        char log_start_msg[64];
        sprintf(log_start_msg , "All %d players connected. Game starting." , shm->player_count);
        log_event("SYSTEM" , log_start_msg , NULL);

        // Wait for game to finish
        pthread_join(tid, NULL);
        
        // Game ended - ask if they want to play again
        printf("\n************************************\n");
        printf("       GAME OVER!\n");
        printf("************************************\n");
        printf("\nPlay again? (y/n): ");
        
        char response;
        scanf(" %c", &response);
        
        if (response == 'y' || response == 'Y') {
            printf("[System] Starting new game...\n");
            log_event("SYSTEM", "Host requested new game", NULL);
            
            // Set flag so children know a new game is coming
            pthread_mutex_lock(&shm->mutex);
            shm->new_game_pending = 1;
            pthread_cond_broadcast(&shm->cond_turn_start);  // Wake any waiting children
            pthread_cond_broadcast(&shm->cond_game_start);
            pthread_mutex_unlock(&shm->mutex);
            
            // Give children time to exit cleanly
            sleep(2);
            
            // Reset game state for new match
            reset_for_new_game();
            play_again = 1;
        } else {
            printf("[System] Shutting down server...\n");
            log_event("SYSTEM", "Host declined new game, shutting down", NULL);
            play_again = 0;
        }
    }
    // ==================== END OUTER GAME LOOP ====================
    
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
    // 1. CREATE THE SHARED MEMORY REGION, Changes made here are visible to all forked processes.
    shm = mmap(NULL, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    
    // 2. CONFIGURE "PROCESS-SHARED" LOCKS
    // Attributes to allow mutexes to work across FORK
    pthread_mutexattr_t mattr; 
    pthread_condattr_t cattr;

    //// Initialize the attribute variables
    pthread_mutexattr_init(&mattr); 
    pthread_condattr_init(&cattr);

    // Tell the attributes that these locks must work across processes.
    //  Without THIS, the locks will be ignored by child processes.
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);


    // 3. INITIALIZE THE ACTUAL LOCKS
    pthread_mutex_init(&shm->mutex, &mattr);   //Main game Lock
    pthread_cond_init(&shm->cond_game_start, &cattr); //Signal: "Game has started"
    pthread_cond_init(&shm->cond_turn_start, &cattr); //Signal: "Your turn"
    pthread_cond_init(&shm->cond_turn_end, &cattr); //Signal : Turn Finished
    pthread_mutex_init(&shm->log_mutex , &mattr); //lock for logs
    pthread_cond_init(&shm->log_cond , &cattr); //Signal:"New Log Added"

    // 4. Set Default game Values
    shm->current_turn_index = -1; // -1 indicates no one is playing yet
    shm->game_running = 1; 
    shm->game_started = 0;  
    shm->turn_completed = 1;
    shm->new_game_pending = 0;
    shm->match_over = 0;
    shm->match_winner_id = -1;
    memset(shm->match_winner_name, 0, sizeof(shm->match_winner_name));
    shm->log_head = 0;
    shm->log_tail = 0;

    pick_new_word();

    // Loop through all player slots and wipe them clean
    for(int i=0; i<MAX_PLAYERS; i++) { 
        shm->players[i].is_active = 0; 
        shm->players[i].score = 0; 
        shm->players[i].missed_turns = 0;
        shm->players[i].pid = 0;
    }
}

// Resets game state for a new match (called between games)
void reset_for_new_game() {
    pthread_mutex_lock(&shm->mutex);
    
    // Reset player session scores (NOT persistent scoreboard)
    for (int i = 0; i < MAX_PLAYERS; i++) {
        shm->players[i].score = 0;
        shm->players[i].is_active = 0;
        shm->players[i].missed_turns = 0;
        shm->players[i].pid = 0;
    }
    
    // Reset game state flags
    shm->current_turn_index = -1;
    shm->game_running = 1;
    shm->game_started = 0;
    shm->turn_completed = 1;
    shm->new_game_pending = 0;
    shm->match_winner_id = -1;
    
    pick_new_word();
    
    pthread_mutex_unlock(&shm->mutex);
    log_event("SYSTEM", "Game state reset for new match", NULL);
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
        
        // CHECK: Did someone win the match? (host decision needed)
        if (shm->match_over) {
            pthread_mutex_unlock(&shm->mutex);
            
            // Prompt host for decision (runs in main process)
            printf("\n************************************\n");
            printf("   MATCH OVER! %s WINS!\n", shm->match_winner_name);
            printf("************************************\n");
            printf("\nPlay again? (y/n): ");
            
            char response;
            scanf(" %c", &response);
            
            pthread_mutex_lock(&shm->mutex);
            if (response == 'y' || response == 'Y') {
                printf("[System] Starting new match...\n");
                log_event("SYSTEM", "Host requested new match", NULL);
                
                // Reset scores for new match
                for (int i = 0; i < shm->player_count; i++) {
                    shm->players[i].score = 0;
                }
                shm->new_game_pending = 1;  // Signal for new match
                shm->match_over = 0;
                shm->match_winner_id = -1;
                pick_new_word();
                
                // Wake all children to continue
                pthread_cond_broadcast(&shm->cond_turn_start);
            } else {
                printf("[System] Ending game...\n");
                log_event("SYSTEM", "Host declined new match, shutting down", NULL);
                shm->game_running = 0;
                shm->match_over = 0;
                
                // Wake all children so they can send GAME_OVER to clients
                pthread_cond_broadcast(&shm->cond_turn_start);
            }
            pthread_mutex_unlock(&shm->mutex);
            continue;  // Re-check game_running
        }
        
        //skipping inactive player
        int checked = 0;
        while (shm->players[current_idx].is_active == 0) {
            printf("[Scheduler] Skipping inactive Player %d\n", current_idx + 1);
            current_idx = (current_idx + 1) % shm->player_count;
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
        pthread_cond_broadcast(&shm->cond_turn_start);

        // Wait for Turn End
        while (shm->turn_completed == 0 && shm->players[current_idx].is_active && !shm->match_over) {
            pthread_cond_wait(&shm->cond_turn_end, &shm->mutex);
        }

        log_event("SCHEDULER" , "Turn completed, advancing..." , NULL);
        current_idx = (current_idx + 1) % shm->player_count;
        pthread_mutex_unlock(&shm->mutex);
    }

    //A broadcast to signal everyone game is ending
    pthread_mutex_lock(&shm->mutex);
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
    pthread_mutex_lock(&shm->mutex);
    while (!shm->game_started && !shm->new_game_pending) {
        pthread_cond_wait(&shm->cond_game_start, &shm->mutex);
    }
    // Check if a new game was requested while waiting
    if (shm->new_game_pending) {
        pthread_mutex_unlock(&shm->mutex);
        close(socket);
        exit(0);  // Exit cleanly so new process can be forked
    }
    pthread_mutex_unlock(&shm->mutex);

    // Tell client "Game is starting"
    int sig = SIGNAL_GAME_START;
    send(socket, &sig, sizeof(int), 0);

    // MAIN SESSION LOOP
    while (1) {
        pthread_mutex_lock(&shm->mutex);
        
        // WAIT: Sleep until it is MY turn
        while ((shm->current_turn_index != player_id || shm->turn_completed == 1) 
               && shm->game_running 
               && !shm->new_game_pending
               && !shm->match_over) {
            pthread_cond_wait(&shm->cond_turn_start, &shm->mutex);
        }
        
        // Check: Game ending (host said NO)
        if (!shm->game_running) { 
            pthread_mutex_unlock(&shm->mutex);
            
            // Send GAME_OVER to THIS client
            int sig = SIGNAL_GAME_OVER;
            send(socket, &sig, sizeof(int), 0);
            send(socket, shm->match_winner_name, 20, 0);
            
            goto disconnect;  // Exit cleanly
        }
        
        // Check: New match starting (host said YES)
        if (shm->new_game_pending) {
            // Send NEW_GAME signal to THIS client
            int sig = SIGNAL_NEW_GAME;
            send(socket, &sig, sizeof(int), 0);
            send(socket, shm->match_winner_name, 20, 0);
            
            shm->new_game_pending = 0;  // Clear for this child
            pthread_mutex_unlock(&shm->mutex);
            continue;  // Go back to waiting for turn
        }
        
        // Check: Match winner decided by another player, wait for decision
        if (shm->match_over && shm->match_winner_id != player_id) {
            pthread_mutex_unlock(&shm->mutex);
            continue;  // Keep waiting for scheduler decision
        }
        
        log_event("CHILD", "it is MY turn, sending data to client...", player_log);
        pthread_mutex_unlock(&shm->mutex); 
        

        // --- MY TURN LOGIC ---
        sig = SIGNAL_YOUR_TURN;
         if (send(socket, &sig, sizeof(int), MSG_NOSIGNAL) <= 0) goto disconnect;


        //Send current score so client can display it
        int current_score = shm->players[player_id].score;
        send(socket, &current_score, sizeof(int), 0);

        char guess[20] = {0};
        char result[20] = {0};
        char log_msg[128];
        
        
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

            char kick_message[64];
            snprintf(kick_message, sizeof(kick_message), "Kicked: %d Missed Turns", MAX_MISSED_TURNS);
            if (strikes >= MAX_MISSED_TURNS) {
                // 3 Strikes -> OUT
                pthread_mutex_unlock(&shm->mutex);
                // Note: client_cleanup handles notifying scheduler/Turn End
                client_cleanup(player_id, kick_message); 
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
        
       // On Round Win
        if (strcmp(result, "GGGGG") == 0) {
        

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
                
                // Set match_over flag for scheduler to see
                shm->match_over = 1;
                shm->match_winner_id = player_id;
                strcpy(shm->match_winner_name, name);
                
                // Signal scheduler that match ended
                shm->turn_completed = 1;
                pthread_cond_signal(&shm->cond_turn_end);
                
                // Wait for scheduler's decision (host prompt)
                while (shm->match_over && shm->game_running) {
                    pthread_cond_wait(&shm->cond_turn_start, &shm->mutex);
                }
                
                // Check what decision was made
                if (!shm->game_running) {
                    // Host said NO - send GAME_OVER to client
                    pthread_mutex_unlock(&shm->mutex);
                    
                    int sig = SIGNAL_GAME_OVER;
                    send(socket, &sig, sizeof(int), 0);
                    send(socket, name, 20, 0);  // Send winner name
                    
                    goto disconnect;  // Exit cleanly
                } else {
                    // Host said YES - send NEW_GAME signal to client
                    int sig = SIGNAL_NEW_GAME;
                    send(socket, &sig, sizeof(int), 0);
                    send(socket, name, 20, 0);  // Send winner name
                    shm->new_game_pending = 0;  // Clear the pending flag
                }
            } else {
                // Round won but match not over - just pick new word
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
    
    // Game loop exited (only happens if game_running becomes 0 or new_game_pending)
    // This path is now only used for server shutdown, not match end

    disconnect:
    pthread_mutex_lock(&shm->mutex);
    shm->players[player_id].is_active = 0;
    shm->turn_completed = 1; // Release scheduler so it doesn't hang
    pthread_cond_signal(&shm->cond_turn_end);
    pthread_mutex_unlock(&shm->mutex);
    close(socket);
    exit(0);
}



// Function to help user find their IP
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