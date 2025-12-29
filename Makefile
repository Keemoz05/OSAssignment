players ?= 3
flags = -pthread

#add comment with hashtag
all: server client createpipes

server: server.c
	gcc $(flags) server.c -o server 

client: client.c
	gcc $(flags) client.c -o client 

createpipes:
	-mkfifo player_pipe 2> /dev/null      

run:all
	./server
kill:
	@echo "Killing all game processes..."
	-pkill -f "./server"
	-pkill -f "./client"
	-rm player_pipe p1 p2 p3 p4 p5
	@echo "Done."

test:
	@echo "Hows your knee"