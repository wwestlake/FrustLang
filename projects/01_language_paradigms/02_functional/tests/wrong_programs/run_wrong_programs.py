#!/usr/bin/env python3
"""Runs the compiler on programs that are wrong and checks it never crashes.

wrong_programs.txt holds many small programs, each starting with a line `=== name`. Some are Rust habits FRust does
not have, some misuse types. The compiler must answer each one with either success or an ordinary error (exit code 0
or 1) - never an abort, a crash or a hang. It runs one program at a time and stops at the first one that misbehaves,
so a crash costs one failure, not a pile of them.

    python run_wrong_programs.py <path to frust_compiler_x.exe> [declarations.frust]

`declarations.frust` (optional) is put in front of every program, for programs that call an application's API.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent


def split_programs(text):
    programs, name, lines = [], None, []
    for line in text.splitlines():
        if line.startswith("=== "):
            if name is not None:
                programs.append((name, "\n".join(lines) + "\n"))
            name, lines = line[4:].strip(), []
        else:
            lines.append(line)
    if name is not None:
        programs.append((name, "\n".join(lines) + "\n"))
    return programs


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    compiler = sys.argv[1]
    prefix = Path(sys.argv[2]).read_text(encoding="utf-8") + "\n" if len(sys.argv) > 2 else ""
    programs = split_programs((HERE / "wrong_programs.txt").read_text(encoding="utf-8"))

    refused = accepted = 0
    with tempfile.TemporaryDirectory() as work:
        for number, (name, source) in enumerate(programs, start=1):
            source_file = Path(work) / f"{name}.fr"
            source_file.write_text(prefix + source, encoding="utf-8")
            try:
                done = subprocess.run([compiler, "--emit-obj", str(Path(work) / f"{name}.o"), str(source_file)],
                                      capture_output=True, text=True, timeout=30)
            except subprocess.TimeoutExpired:
                print(f"FAIL  test {number} ({name}): the compiler hung")
                return 1
            if done.returncode not in (0, 1):
                print(f"FAIL  test {number} ({name}): the compiler crashed (exit code {done.returncode})")
                print((done.stdout + done.stderr).strip()[:400])
                return 1
            if done.returncode == 1:
                refused += 1
            else:
                accepted += 1

    print(f"PASS  {len(programs)} wrong programs: {refused} refused with an error, {accepted} accepted, no crashes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
