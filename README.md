# Project 2 - Overclocked

* Run the following commands to compile and run the program.
```
make
./overclocked < test-cases/input_00.txt
```

* Run the following command to grade the program. It builds the program, runs every test case
  (with a 5-second time limit each), writes your outputs to `my-outputs/`, and scores them.
```
make grade
```

* Run the following command to only generate your outputs in `my-outputs/` without grading.
```
make testcases
```

* Run the following command to check a single test case.
```
python3 test/checker.py <executable> <input_file> <output_file> <expected_output_file>
```

* Run the following command to grade the program with given paths.
```
python3 test/grader.py <executable-path> <test-cases-path>
```

* Run the following command to remove the executable, object files and your outputs.
```
make clean
```
