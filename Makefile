ifeq ($(PROGRAM),)
error:
	@printf "$(BRED)Specify build target!\n"
endif

EXECUTABLE = build/$(PROGRAM)
TEST_EXEC = test/$(PROGRAM)

# By default, build executable:
# NOTE: first target in the file is the default.
default: $(EXECUTABLE)

#-----------------------
# Compiler/linker flags
#-----------------------

CC = gcc

# Compiler flags:
CFLAGS = \
	-std=c2x \
	-Wall    \
	-Wextra

# Linker flags:
LDFLAGS = -pthread -lrt -lm

# Select build mode:
# NOTE: invoke with "DEBUG=1 make" or "make DEBUG=1".
ifeq ($(DEBUG),1)
	# Add default symbols:
	CFLAGS += -g
else
	# Enable link-time optimization:
	CFLAGS  += -flto
	LDFLAGS += -flto
endif

#--------
# Colors
#--------

# Use ANSI color codes:
BRED    = \033[1;31m
BGREEN  = \033[1;32m
BYELLOW = \033[1;33m
GREEN   = \033[1;35m
BCYAN   = \033[1;36m
RESET   = \033[0m

#-------------
# Copied file
#-------------

DUMMY_DST = build/dummy_dst

#-------------------
# Build/run process
#-------------------

build/%: %.c
	@printf "$(BYELLOW)Building program $(BCYAN)$<$(RESET)\n"
	@mkdir -p build
	$(CC) $< $(CFLAGS) -o $@ $(LDFLAGS)

run: $(EXECUTABLE)
	@./$(EXECUTABLE) $(DUMMY_DST)

# Timing command usage:
TIME_CMD    = /usr/bin/time
TIME_FORMAT = \
	"CPU Percentage: %P\nReal time: %e sec\nUser time: %U sec"

time: $(EXECUTABLE) $(DUMMY_DST)
	@$(TIME_CMD) --quiet --format=$(TIME_FORMAT) $(EXECUTABLE) $(DUMMY_DST) | cat


TEST_COUNTING=0
ARGS=


test_clean:
	@printf "$(BYELLOW)Cleaning test directory$(RESET)\n"
	@rm -rf test


##################################################################################################
ADDR=127.0.0.1
PORT=1337
TIME=10
NODES=1
LCOV_FLAGS=--coverage -fcondition-coverage

main: clean libcounting build_manager build_worker

lcov__: clean 
	gcc -c -fPIC lib/manager.c -o lib/manager.o --coverage
	gcc -c -fPIC lib/worker.c -o lib/worker.o --coverage
	gcc -shared lib/manager.o lib/worker.o -o lib/libcounting.so
	rm lib/manager.o lib/worker.o
	gcc --coverage -fcondition-coverage lib/manager.c test_manager.c -o build/manager -lm
	gcc --coverage -fcondition-coverage lib/worker.c test_worker.c -o build/worker -lm
	build/manager $(ADDR) $(PORT) $(TIME) $(NODES) &
	build/worker $(ADDR) $(PORT) &
	lcov --branch-coverage --mcdc-coverage --gcov-tool=gcov -d . -c -o lcov.info
	genhtml --function-coverage --branch-coverage --mcdc-coverage lcov.info

lcov: clean 
	gcc --coverage -fcondition-coverage lib/manager.c test_manager.c -o build/manager -lm
	gcc --coverage -fcondition-coverage lib/worker.c test_worker.c -o build/worker -lm
	build/manager $(ADDR) $(PORT) $(TIME) 2 &
	build/worker $(ADDR) $(PORT) &
	build/worker $(ADDR) $(PORT) &
	lcov --branch-coverage --mcdc-coverage --gcov-tool=gcov -d . -c -o lcov.info
	genhtml --function-coverage --branch-coverage --mcdc-coverage lcov.info

build_manager: libcounting
	gcc -c test_manager.c -lm -o build/test_manager.o
	gcc build/test_manager.o -L lib -lcounting -L /usr/lib -lm -o build/manager

build_worker: libcounting
	gcc -c test_worker.c -lm -o build/test_worker.o
	gcc build/test_worker.o  -L lib -lcounting -L /usr/lib -lm -o build/worker

libcounting:
	gcc -c -fPIC lib/manager.c -o lib/manager.o
	gcc -c -fPIC lib/worker.c -o lib/worker.o
	gcc -shared lib/manager.o lib/worker.o -o lib/libcounting.so
	rm lib/manager.o lib/worker.o

manager: build_manager
	LD_LIBRARY_PATH=lib build/manager $(ADDR) $(PORT) $(TIME) $(NODES)

worker: build_worker
	LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT)

test: main
	# @LD_LIBRARY_PATH=lib build/manager $(ADDR) $(PORT) $(TIME) $(NODES) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @echo "TEST 1: COMPLETED"
	# @time LD_LIBRARY_PATH=lib build/manager $(ADDR) $(PORT) $(TIME) 2 &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @echo "TEST 2: COMPLETED"
	@time LD_LIBRARY_PATH=lib build/manager $(ADDR) $(PORT) $(TIME) 1 &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	@LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# @LD_LIBRARY_PATH=lib build/worker $(ADDR) $(PORT) &
	# sleep 1
	# @echo "TEST 3: COMPLETED"


##################################################################################################

TEST: clean
	gcc manager.c test_manager.c -o manager -lm
	gcc worker.c test_worker.c -lm -o worker
	./manager 127.0.0.1 1337 10 4 &
	./worker 127.0.0.1 1337 &
	./worker 127.0.0.1 1337 &
	./worker 127.0.0.1 1337 &
	./worker 127.0.0.1 1337 &

lcov_: clean
	gcc --coverage -fcondition-coverage manager.c test_manager.c -o manager -lm
	gcc --coverage -fcondition-coverage worker.c test_worker.c -o worker -lm
	./manager 127.0.0.1 1337 10 1 &
	./worker 127.0.0.1 1337 &
	lcov --branch-coverage --mcdc-coverage --gcov-tool=gcov -d . -c -o lcov.info
	genhtml --function-coverage --branch-coverage --mcdc-coverage lcov.info

	

#---------------
# Miscellaneous
#---------------

clean:
	@printf "$(BYELLOW)Cleaning build directory$(RESET)\n"
	@rm -rf *.gcda *.gcno build/* *.o *.so *.info *.html *.png *.css cmd_line lib/*.so lib/*.o lib/*.gcno

# List of non-file targets:
.PHONY: run clean default
