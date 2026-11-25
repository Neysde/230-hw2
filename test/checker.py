import os, sys, subprocess
try:
    import resource
except ImportError:  # not available on Windows
    resource = None

# Expected output: 3 timelines (Prep, Cook, Plate) followed by the serve-times line.
TIMELINE_LINES = 3
IDLE = '.'

TIMEOUT_SECONDS = 5
MAX_OUTPUT_BYTES = 1 << 20  # correct output is a few KB; this stops runaway print loops


def _limit_output_size():
    resource.setrlimit(resource.RLIMIT_FSIZE, (MAX_OUTPUT_BYTES, MAX_OUTPUT_BYTES))


def run_executable(executable: str, input_file: str, output_file: str,
                   timeout: float = TIMEOUT_SECONDS) -> int:
    """Run the executable with input_file as stdin and stdout written to output_file.

    Returns the exit status (negative if killed by a signal).
    Raises subprocess.TimeoutExpired if it runs longer than timeout seconds.
    """
    with open(input_file, 'r') as fin, open(output_file, 'w') as fout:
        return subprocess.run(
            [os.path.abspath(executable)], stdin=fin, stdout=fout,
            stderr=subprocess.DEVNULL, timeout=timeout,
            preexec_fn=_limit_output_size if resource else None,
        ).returncode


class CustomError(Exception):
    def __init__(self, message):
        self.message = f"\033[91;1;4m{message}:\033[0m"
        super().__init__(self.message)


def timeline_score(output: str, expected: str) -> float:
    """Tick-by-tick comparison that only counts busy ticks.

    A position is scored if either the expected or the student timeline has a
    dish there; idle ticks that both leave idle are not counted, so printing
    only dots earns nothing. Length (T) errors are caught by the serve line.
    """
    busy = correct = 0
    for i in range(max(len(output), len(expected))):
        out_ch = output[i] if i < len(output) else IDLE
        exp_ch = expected[i] if i < len(expected) else IDLE
        if out_ch == IDLE and exp_ch == IDLE:
            continue
        busy += 1
        if out_ch == exp_ch:
            correct += 1
    return correct / busy if busy else 1.0


def serve_score(output: str, expected: str) -> float:
    """Entry-by-entry comparison of 'ID=time' tokens, in input order."""
    out_entries = output.split()
    exp_entries = expected.split()
    correct = sum(o == e for o, e in zip(out_entries, exp_entries))
    return correct / max(len(out_entries), len(exp_entries))


def check_output(output_file_name: str, expected_output_file_name: str) -> float:

    output_file = open(output_file_name, 'r')
    expected_output_file = open(expected_output_file_name, 'r')

    output = output_file.read().replace('\x00', '').strip().split('\n')
    expected_output = expected_output_file.read().strip().split('\n')

    output = [line.strip() for line in output if line.strip()]
    expected_output = [line for line in expected_output if line.strip()]

    print(output)
    print(expected_output)

    line_scores = []
    for i, expected_line in enumerate(expected_output):
        if i >= len(output):
            line_scores.append(0.0)
        elif i < TIMELINE_LINES:
            line_scores.append(timeline_score(output[i], expected_line))
        else:
            line_scores.append(serve_score(output[i], expected_line))

    print('line scores:', ' '.join(f'{s:.2f}' for s in line_scores))

    return sum(line_scores) / len(expected_output)


if __name__ == '__main__':
    if len(sys.argv) != 5:
        print('Usage: python3 checker.py <executable> <input_file> <output_file> <expected_output_file>')
        sys.exit(1)

    executable, input_file, output_file, expected_output_file = sys.argv[1:]
    try:
        exit_status = run_executable(executable, input_file, output_file)
        if exit_status != 0:
            print(f'warning: program exited with status {exit_status}')
        score = check_output(output_file, expected_output_file)
    except subprocess.TimeoutExpired:
        print(f'timed out after {TIMEOUT_SECONDS} s')
        score = 0
    print(f'{score * 100:.2f} points')
