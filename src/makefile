all: compile

compile:
	@gcc -o main.run *.c -Wall -I . -lshlwapi

compile-profile:
	@gcc -o main.run *.c -Wall -I . -lshlwapi -pg -no-pie

compile-debug:
	@gcc -o main.run *.c -DDEBUG -Wall -I . -lshlwapi

compile-debugger:
	@gcc -o main-debugger.run *.c -Wall -I . -lshlwapi -g 

compile-debug-logs:
	@gcc -o main.run *.c -DDEBUG_LOGS -Wall -I . -lshlwapi

compile-debug-gc:
	@gcc -o main.run *.c -DDEBUG_GC -Wall -I . -lshlwapi

compile-stress-gc:
	@gcc -o main.run *.c -DDEBUG_STRESS_GC -Wall -I . -lshlwapi

compile-debug-and-debugger:
	@gcc -o main-debugger.run *.c -DDEBUG -Wall -I . -lshlwapi -g

run:
	@./main.run ./program.simpl

run-repl:
	@./main.run

exec:
	@make compile --no-print-directory && make run --no-print-directory

exec-profile:
	@make compile-profile --no-print-directory && ./main.run program.simpl && gprof main.run gmon.out > cpu-profile.out && rm gmon.out

exec-debug:
	@make compile-debug --no-print-directory && make run --no-print-directory

exec-debug-logs:
	@make compile-debug-logs --no-print-directory && make run --no-print-directory

exec-debug-gc:
	@make compile-debug-gc --no-print-directory && make run --no-print-directory

exec-stress-gc:
	@make compile-stress-gc --no-print-directory && make run --no-print-directory

build:
	@make compile && cp ./main.run ../build/simpl.exe && make compile-stress-gc && cp ./main.run ../build/simpl-stress-gc.exe