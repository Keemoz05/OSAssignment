players ?= 3
flags = -pthread

#add comment with hashtag
all: client socket 

socket: socket.c
	gcc $(flags) socket.c  -o socket  

client: client.c
	gcc $(flags) client.c -o client 


run:all
	./socket
kill:
	@echo "Killing all game processes..."
	-pkill -f "\./socket"
	-pkill -f "\./client"
	@echo "Done."


clean-logs:
	@echo "Removing all log files..."
	-rm -f server_log.txt
	-rm -f log_*.txt
	-rm -f game.log
	-rm -f scores.txt
	@echo "Logs cleared."


test:
	@echo "Hows your knee"