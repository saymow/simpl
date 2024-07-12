all: compile

compile:
	@gcc -o main.run *.c */*.c  -Wall -I . -lshlwapi

compile-profile:
	@gcc -o main.run *.c  */*.c -Wall -I . -lshlwapi -pg -no-pie

compile-debug:
	@gcc -o main.run *.c  */*.c -DDEBUG -Wall -I . -lshlwapi

compile-debugger:
	@gcc -o main-debugger.run *.c  */*.c -Wall -I . -lshlwapi -g 

compile-debug-logs:
	@gcc -o main.run *.c  */*.c -DDEBUG_LOGS -Wall -I . -lshlwapi

compile-debug-gc:
	@gcc -o main.run *.c  */*.c -DDEBUG_GC -Wall -I . -lshlwapi

run:
	@./main.run ./simpl.in

run-repl:
	@./main.run

exec:
	@make compile --no-print-directory && make run --no-print-directory

exec-profile:
	@make compile-profile --no-print-directory && ./main.run simpl.in && gprof main.run gmon.out > cpu-profile.out && rm gmon.out

exec-debug:
	@make compile-debug --no-print-directory && make run --no-print-directory

exec-debug-logs:
	@make compile-debug-logs --no-print-directory && make run --no-print-directory

exec-debug-gc:
	@make compile-debug-gc --no-print-directory && make run --no-print-directory

build:
	@make compile && cp ./main.run ../build/simpl.exe