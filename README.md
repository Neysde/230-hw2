# Bushell

Bushell is an in-memory Unix-like shell written in C. The project currently
contains the shell data-structure architecture and a placeholder command
processor. Every command except `exit` prints `default output`.

## Requirements

- GCC
- GNU Make
- Python 3

## Build

Build the shell with:

```sh
make
```

This creates the `bushell` executable using:

```sh
gcc -Wall -Wextra -pedantic -o bushell src/main.c
```

## Run

Start the shell with:

```sh
./bushell
```

The shell displays a prompt for each input line. Type `exit` to terminate the
program. Other input currently produces:

```text
default output
```

Commands can also be provided from a file or through standard input:

```sh
./bushell < test-cases/input1.txt
```

## Test

Run the included test cases with:

```sh
make grade
```

The grader runs every `test-cases/input*.txt` file, removes interactive prompt
text, saves the sanitized output in `my_outputs/`, and compares it with the
corresponding expected output file.

## Project Layout

```text
src/main.c          Main shell scaffold and command-processing entry point
src/template.c      Architecture template for future implementation
test/grader.py      Local test runner and output comparison tool
test-cases/         Input and expected-output fixtures
my_outputs/         Generated grader output
Makefile            Build, test, and clean commands
```

## Clean

Remove the compiled executable with:

```sh
make clean
```
