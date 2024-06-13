all: compile

compile:
	@gcc -o main.run *.c  -Wall -I .

run:
	@./main.run