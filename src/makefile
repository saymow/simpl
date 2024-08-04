FLAGS = -lshlwapi -I .
DEV_FLAGS = -Wall
WARNING_FLAGS = -Wextra -Wpedantic
PROFILE_FLAGS = -pg -no-pie
DEBUG_FLAGS = -g
OPTIMIZATION_FLAGS = -O2

# Program debug flags
PROGRAM_DEBUG = -DDEBUG							# Enable all program debug features
PROGRAM_DEBUG_LOGS = -DDEBUG_LOGS    			# Enable program stack and bytecode logs
PROGRAM_DEBUG_GC = -DDEBUG_GC					# Enable GC stress and GC logs
PROGRAM_DEBUG_GC_STRESS = -DDEBUG_STRESS_GC		# Enable GC stress

all: compile

compile:
	@gcc -o main.run *.c $(FLAGS) $(DEV_FLAGS)

compile-verbose-warnings:
	@gcc -o main.run *.c $(FLAGS) $(DEV_FLAGS) $(WARNING_FLAGS)

compile-profile:
	@gcc -o main.run *.c $(FLAGS) $(DEV_FLAGS) $(PROFILE_FLAGS)

compile-debug:
	@gcc -o main.run *.c $(FLAGS) $(DEV_FLAGS) $(PROGRAM_DEBUG)  							 

compile-debugger:
	@gcc -o main-debugger.run *.c $(FLAGS) $(DEV_FLAGS) $(DEBUG_FLAGS) 

compile-debug-logs:
	@gcc -o main.run *.c $(FLAGS) $(DEV_FLAGS) $(PROGRAM_DEBUG_LOGS)

compile-debug-gc:
	@gcc -o main.run *.c $(FLAGS) $(DEV_FLAGS) $(PROGRAM_DEBUG_GC)

compile-stress-gc:
	@gcc -o main.run *.c $(FLAGS) $(DEV_FLAGS) $(PROGRAM_DEBUG_GC_STRESS)

compile-debug-and-debugger:
	@gcc -o main-debugger.run *.c $(FLAGS) $(DEV_FLAGS) $(DEBUG_FLAGS) $(PROGRAM_DEBUG)

compile-optimized:
	@gcc -o main.run *.c $(FLAGS) $(OPTIMIZATION_FLAGS)

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
	@make compile-debug-gc && cp ./main.run ../build/simpl-stress-gc.exe && make compile-optimized && cp ./main.run ../build/simpl.exe