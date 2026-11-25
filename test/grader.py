import os, sys, subprocess
from checker import check_output, run_executable, TIMEOUT_SECONDS

def get_grade(executable: str, test_cases_folder: str) -> float:

    grade = 0
    number_of_test_cases = 0

    if not os.path.exists('my-outputs'):
        os.makedirs('my-outputs')

    os.system('make')

    for input_file in sorted(os.listdir(f'{test_cases_folder}')):
        if not input_file.startswith('input'):
            continue
        output_file = input_file.replace('input', 'output')
        number_of_test_cases += 1

        print(f'=== {input_file}', flush=True)
        try:
            exit_status = run_executable(executable, f'{test_cases_folder}/{input_file}',
                                         f'my-outputs/{output_file}')
            if exit_status != 0:
                print(f'warning: program exited with status {exit_status}')
            test_grade = check_output(f'my-outputs/{output_file}', f'{test_cases_folder}/{output_file}')
        except subprocess.TimeoutExpired:
            print(f'timed out after {TIMEOUT_SECONDS} s')
            test_grade = 0
        except Exception as e:
            print(e)
            test_grade = 0

        grade += test_grade

        sys.stdout.flush()
        print(f"\033[93mtest-case '{input_file}': {test_grade*100:.2f} points\033[0m", file=sys.stderr)

    if grade ==0:
        return 0
    else:
        return grade * 100 / number_of_test_cases

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('Usage: python grader.py <executable> <test_cases_folder>')
        sys.exit(1)

    grade = get_grade(sys.argv[1], sys.argv[2])

    print(f'\033[92mGrade = {grade:.2f}\033[0m', file=sys.stderr)
    print(f'{grade:.2f}')
