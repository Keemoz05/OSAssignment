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

// ======================================================================================
//   SECTION 1: CONSTANTS & SHARED MEMORY DEFINITIONS
// ======================================================================================

#define PORT 8080
#define MAX_PLAYERS 4
#define WORD_LENGTH 5
#define SIGNAL_GAME_START 1
#define SIGNAL_YOUR_TURN  2

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
    int score;
} PlayerData;

// THE SHARED MEMORY OBJECT
// Everything in here is visible to Parent and all Children
typedef struct {
    PlayerData players[MAX_PLAYERS];
    int player_count;    // Total players expected
    char target_word[6]; // The secret word
    
    // Game State Flags
    int current_turn_index; // 0 to 3, indicates who plays now
    int game_running;       // 1 = Game On, 0 = Game Over
    int game_started;       // 1 = All players connected, start allowed
    int turn_completed;     // Handshake flag to prevent double-turns
    
    // POSIX Synchronization Primitives (Must be process-shared)
    pthread_mutex_t mutex;           // Main lock for reading/writing shared memory
    pthread_cond_t cond_game_start;  // "Barrier": wait here until lobby is full
    pthread_cond_t cond_turn_start;  // Signal: "Wake up and check if it's your turn"
    pthread_cond_t cond_turn_end;    // Signal: "I finished my turn, Scheduler can proceed"
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


// ======================================================================================
//   SECTION 2: MAIN ENTRY POINT
// ======================================================================================

int main() {
    srand(time(NULL)); 
    signal(SIGPIPE, SIG_IGN); // Prevent server crash if client abruptly disconnects

    int server_fd, mode;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // 1. Initialize Shared Memory (mmap)
    setup_shared_memory();

    // 2. User Configuration
    printf("--- WORD GUESS SERVER ---\n");
    printf("1. Local Mode (Auto-launch terminals)\n");
    printf("2. Network Mode (Wait for remote connections)\n");
    printf("Select: ");
    if (scanf("%d", &mode) != 1) return 1;

    printf("Enter number of players (Max %d): ", MAX_PLAYERS);
    if (scanf("%d", &shm->player_count) != 1) return 1;

    // 3. Start the Referee (Scheduler) Thread
    // This thread runs in the background of the Parent Process
    pthread_t tid;
    pthread_create(&tid, NULL, scheduler_thread_func, NULL);

    // 4. Socket Boilerplate
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Bind to 0.0.0.0 (All interfaces)
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed"); exit(1);
    }
    listen(server_fd, MAX_PLAYERS);

    // 5. Handling Launch Modes
    if (mode == 1) {
        // LOCAL: Fork and exec xterms automatically
        printf("[System] Launching %d local client terminals...\n", shm->player_count);
        for (int i = 0; i < shm->player_count; i++) {
            if (fork() == 0) {
                char title[20];
                sprintf(title, "Player %d", i + 1);
                // Pass "127.0.0.1" as an argument to client so it doesn't ask user
                execlp("xterm", "xterm", "-T", title, "-e", "./client", "127.0.0.1", NULL);
                exit(0);
            }
        }
    } else {
        // NETWORK: Print IP and wait
        printf("\n[System] Network Mode Enabled.\n");
        print_local_ip();
        printf("Ask %d players to connect to the IP above.\n", shm->player_count);
    }

    // 6. The Accept Loop (Multiprocessing Logic)
    int connected_count = 0;
    while (connected_count < shm->player_count) {
        printf("[System] Waiting for players: %d/%d connected...\n", connected_count, shm->player_count);
        
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) continue;

        // CRITICAL: Fork a new process for the connection
        if (fork() == 0) {
            // --- CHILD PROCESS CODE ---
            close(server_fd); // Child doesn't need the listener
            handle_client_session(new_socket, connected_count);
            exit(0); // Kill child when session ends
        } else {
            // --- PARENT PROCESS CODE ---
            close(new_socket); // Parent doesn't need the specific client socket
            connected_count++;
        }
    }
    
    printf("[System] All players connected. Main process waiting for Scheduler.\n");
    pthread_join(tid, NULL); // Wait for game to end
    
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

    // Defaults
    shm->current_turn_index = -1;
    shm->game_running = 1;
    shm->game_started = 0; 
    shm->turn_completed = 1; 
    pick_new_word();
    for(int i=0; i<MAX_PLAYERS; i++) { shm->players[i].is_active = 0; shm->players[i].score = 0; }
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
    pthread_cond_broadcast(&shm->cond_game_start); // WAKE UP CALL
    pthread_mutex_unlock(&shm->mutex);
    
    // 3. GAME LOOP: Manage turns
    int current_idx = 0;
    while (shm->game_running) {
        pthread_mutex_lock(&shm->mutex);

        // A. Find next active player (skip disconnected ones)
        int checked = 0;
        while (!shm->players[current_idx].is_active) {
            current_idx = (current_idx + 1) % shm->player_count;
            checked++;
            if (checked > shm->player_count) {
                shm->game_running = 0; // No one left
                pthread_mutex_unlock(&shm->mutex);
                return NULL;
            }
        }

        // B. Announce Turn
        shm->current_turn_index = current_idx;
        shm->turn_completed = 0; // Reset "Turn Done" flag
        
        printf("[Scheduler] Turn: Player %d | Word: %s\n", current_idx + 1, shm->target_word);
        pthread_cond_broadcast(&shm->cond_turn_start); // Wake up the specific player

        // C. Wait for turn completion
        // We sleep here until the child process signals 'cond_turn_end'
        while (shm->turn_completed == 0 && shm->players[current_idx].is_active) {
            pthread_cond_wait(&shm->cond_turn_end, &shm->mutex);
        }

        // D. Advance
        current_idx = (current_idx + 1) % shm->player_count;
        pthread_mutex_unlock(&shm->mutex);
    }
    return NULL;
}


// ======================================================================================
//   SECTION 5: THE CHILD PROCESS (PLAYER HANDLER)
// ======================================================================================

void handle_client_session(int socket, int player_id) {
    char name[20];
    // Basic handshake
    if (recv(socket, name, 20, 0) <= 0) { close(socket); return; }
    name[strcspn(name, "\n")] = 0;

    // Register into Shared Memory
    pthread_mutex_lock(&shm->mutex);
    strcpy(shm->players[player_id].name, name);
    shm->players[player_id].id = player_id;
    shm->players[player_id].is_active = 1;
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
        
        pthread_mutex_unlock(&shm->mutex); 

        // --- MY TURN LOGIC ---
        sig = SIGNAL_YOUR_TURN;
        if (send(socket, &sig, sizeof(int), MSG_NOSIGNAL) <= 0) goto disconnect;

        char guess[20] = {0};
        char result[20] = {0};
        
        // Wait for user input
        if (recv(socket, guess, sizeof(guess), 0) <= 0) goto disconnect;
        guess[strcspn(guess, "\n")] = 0;

        // Process Guess (Requires Lock for Shared Word)
        pthread_mutex_lock(&shm->mutex);
        evaluate_guess(guess, shm->target_word, result);
        pthread_mutex_unlock(&shm->mutex);

        // Send Feedback
        send(socket, result, strlen(result) + 1, 0);
        printf("[Player %s] Guessed: %s | Result: %s\n", name, guess, result);

        // Win Check
        if (strcmp(result, "GGGGG") == 0) {
            pthread_mutex_lock(&shm->mutex);
            shm->players[player_id].score++;
            printf("!!! %s WON! Score: %d !!!\n", name, shm->players[player_id].score);
            pick_new_word(); // Reset board
            pthread_mutex_unlock(&shm->mutex);
        }

        // END TURN: Signal Scheduler
        pthread_mutex_lock(&shm->mutex);
        shm->turn_completed = 1;
        pthread_cond_signal(&shm->cond_turn_end); // "I'm done, referee!"
        pthread_mutex_unlock(&shm->mutex);
    }
    close(socket);
    return;

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