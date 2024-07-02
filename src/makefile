all: compile

compile:
	@gcc -o main.run *.c */*.c  -Wall -I .

compile-debug:
	@gcc -o main.run *.c  */*.c -DDEBUG -Wall -I .

compile-debug-logs:
	@gcc -o main.run *.c  */*.c -DDEBUG_LOGS -Wall -I .

compile-debug-gc:
	@gcc -o main.run *.c  */*.c -DDEBUG_GC -Wall -I .

run:
	@./main.run ./simpl.in

run-repl:
	@./main.run

exec:
	@make compile --no-print-directory && make run --no-print-directory

exec-debug:
	@make compile-debug --no-print-directory && make run --no-print-directory

exec-debug-logs:
	@make compile-debug-logs --no-print-directory && make run --no-print-directory

exec-debug-gc:
	@make compile-debug-gc --no-print-directory && make run --no-print-directory

build:
	@make compile && cp ./main.run ../build/simpl.exe