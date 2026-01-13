players ?= 3
flags = -pthread

#add comment with hashtag
all: client socket 

socket: socket.c wordbank.c
	gcc $(flags) socket.c wordbank.c -o socket  

client: client.c
	gcc $(flags) client.c -o client 


run:all
	./socket
kill:
	@echo "Killing all game processes..."
	-pkill -f "\./socket"
	-pkill -f "\./client"
	@echo "Done."

test:
	@echo "Hows your knee"