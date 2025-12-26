players ?= 3
flags = -pthread

#add comment with hashtag
all: server client createpipes

server: server.c
	gcc $(flags) server.c -o server 

client: client.c
	gcc $(flags) client.c -o client 

createpipes:
	-mkfifo player_pipe

run:all
	./server
kill:
	@echo "Killing all game processes..."
	-pkill -f "./server"
	-pkill -f "./client"
	-rm player_pipe
	@echo "Done."

test:
	@echo "Hows your knee"