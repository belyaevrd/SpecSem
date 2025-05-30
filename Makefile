# ifeq ($(PROGRAM),)
# error:
# 	@printf "$(BRED)Specify build target!\n"
# endif

EXECUTABLE = build/$(PROGRAM)
TEST_EXEC = test/$(PROGRAM)

# By default, build executable:
# NOTE: first target in the file is the default.
default: all

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

#-------------------
# Build/run process
#-------------------

# Timing command usage:
TIME_CMD    = /usr/bin/time
TIME_FORMAT = \
	"CPU Percentage: %P\nReal time: %e sec\nUser time: %U sec"

time: $(EXECUTABLE) $(DUMMY_DST)
	@$(TIME_CMD) --quiet --format=$(TIME_FORMAT) $(EXECUTABLE) $(DUMMY_DST) | cat

##################################################################################################
ADDR=127.0.0.1
PORT=1337
TIME=12
NODES=1
CORES=1

all: clean_and_build libcounting build_manager build_worker

lcov: clean_and_build
	@printf "$(BYELLOW)Start $(BCYAN)LCOV testing$(RESET)\n"
	@gcc --coverage lib/manager.c test_manager.c -o build/manager -lm
	@gcc --coverage lib/worker.c test_worker.c -o build/worker -lm
	build/manager $(ADDR) $(PORT) $(TIME) 2 &
	build/worker $(ADDR) $(PORT) $(CORES) &
	build/worker $(ADDR) $(PORT) $(CORES) &
	sleep 10
	@lcov --branch-coverage --gcov-tool=gcov -d . -c -o lcov.info
	@genhtml --function-coverage --branch-coverage lcov.info

build_manager: libcounting
	@printf "$(BYELLOW)Building $(BCYAN)manager$(RESET)\n"
	@gcc -c test_manager.c -lm -o build/test_manager.o
	@gcc build/test_manager.o -L build -lcounting -L /usr/lib -lm -o build/manager
	@rm build/test_manager.o

build_worker: libcounting
	@printf "$(BYELLOW)Building $(BCYAN)worker$(RESET)\n"
	@gcc -c test_worker.c -lm -o build/test_worker.o
	@gcc build/test_worker.o  -L build -lcounting -L /usr/lib -lm -o build/worker
	@rm build/test_worker.o

libcounting:
	@printf "$(BYELLOW)Building $(BCYAN)library$(RESET)\n"
	@gcc -c -fPIC lib/manager.c -o build/manager.o
	@gcc -c -fPIC lib/worker.c -o build/worker.o
	@gcc -shared build/manager.o build/worker.o -o build/libcounting.so
	@rm build/manager.o build/worker.o

manager: build_manager
	LD_LIBRARY_PATH=build build/manager $(ADDR) $(PORT) $(TIME) $(NODES)

worker: build_worker
	for i in $$(seq 1 $(NODES)) ; do \
		time LD_LIBRARY_PATH=build build/worker $(ADDR) $(PORT) $(CORES) & \
	done

test: all
	#1
	@printf "$(BYELLOW)TEST 1:$(RESET)\n"
	for i in $$(seq 1 1) ; do \
		time LD_LIBRARY_PATH=build build/worker $(ADDR) $(PORT) $(CORES) & \
	done
	LD_LIBRARY_PATH=build build/manager $(ADDR) $(PORT) $(TIME) 1 
	#2
	for i in $$(seq 1 2) ; do \
		time LD_LIBRARY_PATH=build build/worker $(ADDR) $(PORT) $(CORES) & \
	done
	@printf "$(BYELLOW)TEST 2:$(RESET)\n"
	LD_LIBRARY_PATH=build build/manager $(ADDR) $(PORT) $(TIME) 2
	#3
	for i in $$(seq 1 3) ; do \
		time LD_LIBRARY_PATH=build build/worker $(ADDR) $(PORT) $(CORES) & \
	done
	@printf "$(BYELLOW)TEST 3:$(RESET)\n"
	LD_LIBRARY_PATH=build build/manager $(ADDR) $(PORT) $(TIME) 3


##################################################################################################

#---------------
# Miscellaneous
#---------------
clean_and_build: clean
	@mkdir build

clean:
	@printf "$(BYELLOW)Cleaning $(BCYAN)build directory$(RESET)\n"
	@rm -rf *.gcda *.gcno build *.o *.so *.info *.html *.png *.css cmd_line build/*.so build/*.o lib/*.gcno SpecSem

# List of non-file targets:
.PHONY: run clean default
