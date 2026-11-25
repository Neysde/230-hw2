# compiler and the flags
CC     := gcc
CFLAGS := -Wall -Wextra -pedantic

# all source and header files in src
SRCS := $(wildcard src/*.c)
HDRS := $(wildcard src/*.h)

# commands (to separate them from files)
.PHONY: default debug grade test memcheck clean

# make builds the shell
default: bushell

# rebuild when any .c or .h in src/ changes
bushell: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

# build with debug info (-g) so valgrind shows line numbers
debug: clean
	$(MAKE) CFLAGS="$(CFLAGS) -g"

# course test cases checked with the course grader
grade: bushell
	python3 test/grader.py ./bushell test-cases $(if $(findstring s,$(MAKEFLAGS)),--quiet,)

# our own test cases checked with the course grader
test: bushell
	python3 test/grader.py ./bushell tests

# runs every course test input under valgrind and stops at the first leak or memory error
memcheck: debug
	@for f in test-cases/input*.txt; do \
		valgrind --leak-check=full --errors-for-leak-kinds=all --error-exitcode=1 -q \
			./bushell < $$f > /dev/null || { echo "memcheck FAILED: $$f"; exit 1; }; \
		echo "memcheck ok: $$f"; \
	done

# removes the executable and any object files
clean:
	rm -f bushell src/*.o *.o src/*.gch