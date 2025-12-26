CC = gcc
CFLAGS = -Wall -pthread

N ?= 3
#use hash to create commands
all: server player createpipe

server: server.c
	$(CC) $(CFLAGS) server.c -o server

player: player.c
	$(CC) $(CFLAGS) player.c -o player

createpipe:
	@# The dash (-) tells Make to ignore error if pipe already exists
	-mkfifo player_pipe

run: all
	@echo "Launching Server..."
	xterm -T "Server" -e "./server" &
	@sleep 1
	@echo "Launching $(N) Players..."
	@for i in $$(seq 1 $(N)); do \
		xterm -T "Player $$i" -e "./player" & \
	done

kill:
	@echo "Killing all game processes..."
	-pkill -f "./server"
	-pkill -f "./player"
	@echo "Done."

clean:
	rm -f server player player_pipe

test:
	@echo "helloworld"
	@echo "Makefile uses TABS, Python uses SPACES. They are opposites!"
