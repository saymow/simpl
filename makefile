all: compile

compile:
	@gcc -o main.run *.c  -Wall -I .

compile-debug:
	@gcc -o main.run *.c  -DDEBUG -Wall -I .

run:
	@./main.run ./simpl.in

run-repl:
	@./main.run

exec:
	@make compile --no-print-directory && make run --no-print-directory

exec-debug:
	@make compile-debug --no-print-directory && make run --no-print-directory