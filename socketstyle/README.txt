================================================================================
                    MULTIPLAYER WORD GUESS GAME
                  Socket-Based Client-Server Game
================================================================================

TABLE OF CONTENTS
-----------------
1. How to Compile (Make) and Run
2. Example Commands
3. Game Rules Summary
4. Modes Supported

================================================================================
1. HOW TO COMPILE (MAKE) AND RUN
================================================================================

PREREQUISITES:
- Linux/Unix environment with GCC compiler
- POSIX threads library (pthread)
- xterm (for Local Mode auto-launch)

RUNNING THE GAME:
-----------------
    make run        # Compile and start the server
    ./socket        # Run server directly (after compiling)
    ./client        # Run client directly (requires server running)
    ./client <IP>   # Connect to specific server IP address

CLEANUP:
--------
    make kill       # Terminate all running game processes
    make clean-logs # Remove all log files (game.log, log_*.txt, scores.txt)

================================================================================
2. EXAMPLE COMMANDS
================================================================================

STARTING A LOCAL GAME (2 PLAYERS):
----------------------------------
    $ make run
    --- WORD GUESS SERVER ---
    1. Local Mode (Auto-launch terminals)
    2. Network Mode (Wait for remote connections)
    Select: 1
    Enter number of players (Max 4): 2

STARTING A NETWORK GAME:
------------------------
    Server side:
    $ make run
    Select: 2
    Enter number of players (Max 4): 3
    [System] Network Mode Enabled.
    Possible IP(s): 192.168.1.100

    Client side (on other machines):
    $ ./client
    Enter Server IP (Press ENTER for Localhost): 192.168.1.100
    Enter your name: Player1

================================================================================
3. GAME RULES SUMMARY
================================================================================

OBJECTIVE:
----------
Guess the secret 5-letter word. First player to win 3 rounds wins the match!

GAMEPLAY:
---------
- Players take turns guessing a 5-letter word
- After each guess, you receive feedback:

    G (Green)  = Correct letter in the correct position
    Y (Yellow) = Correct letter in the wrong position
    X (Gray)   = Letter not in the word

EXAMPLE:
--------
    Target Word: BRAIN
    Your Guess:  BREAD
    Feedback:    GGYXX

    Explanation:
    B -> G (position 1 matches)
    R -> G (position 2 matches)
    E -> X (E not in BRAIN)
    A -> Y (A is in BRAIN but at position 3, not 4)
    D -> X (D not in BRAIN)

SCORING:
--------
- Correctly guess the word     = +1 point
- First to 3 points            = MATCH WINNER!
- Scores persist across games in scores.txt

TURN SYSTEM:
------------
- Turn-based multiplayer (one player guesses at a time)
- Players rotate turns until someone wins a round
- New word selected after each round win

INPUT REQUIREMENTS:
-------------------
- Must be exactly 5 letters (no more, no less)
- Letters only (A-Z, case insensitive)
- No numbers or special characters

================================================================================
4. MODES SUPPORTED
================================================================================

MODE 1: LOCAL MODE
------------------
Description:
    Automatically launches separate xterm windows for each player on the
    same machine. 

Features:
    - Auto-connects clients to localhost (127.0.0.1)
    - Each player gets their own terminal window
    - Requires xterm installed

Use Case:
    Single computer, multiple players sharing screen

-------------------------------------------------------------------------------

MODE 2: NETWORK MODE
-------------------
Description:
    Server displays its IP address and waits for remote players to connect.
    Clients can run on different machines on the same network.

Features:
    - Supports up to 4 simultaneous players
    - Players manually enter server IP or press ENTER for localhost
    - Suitable for LAN parties

Use Case:
    Multiple computers on a network

-------------------------------------------------------------------------------

ADDITIONAL FEATURES
-------------------
- Persistent Leaderboard: Player wins saved to scores.txt
- Game Logging: All events logged to game.log and player-specific log files
- Graceful Shutdown: Ctrl+C saves scores before exiting
- Disconnect Handling: Players can disconnect without crashing the server

================================================================================
                           ARCHITECTURE NOTES
================================================================================

Server Components:
- Main Process: Accepts connections and forks child processes
- Child Processes: One per player for direct communication
- Scheduler Thread: Manages turn rotation and game flow
- Logger Thread: Handles asynchronous log writing

Synchronization:
- Shared Memory (mmap): Game state accessible by all processes
- POSIX Mutexes & Condition Variables: Process-shared synchronization

Communication:
- TCP Sockets on Port 8080
- Signal-based game events (GAME_START, YOUR_TURN, GAME_OVER)

================================================================================
                               WORD BANK
================================================================================

The game includes a database of 20+ five-letter words including:
APPLE, BEACH, BRAIN, BREAD, BRUSH, CHAIR, CHESS, CHORD, CLICK, CLOCK,
CLOUD, DANCE, DIARY, DRINK, DRIVE, EARTH, FEAST, FIELD, FRUIT, GLASS...


