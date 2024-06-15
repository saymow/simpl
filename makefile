all: compile

compile:
	@gcc -o main.run *.c  -Wall -I .

run:
	@./main.run ./simpl.in

run-repl:
	@./main.run
