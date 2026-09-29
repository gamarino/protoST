#!/usr/bin/env python3
"""Self-test of benchmarks/run_benchmarks.py: a benchmark that reports a wrong
result, reports nothing, or crashes must fail the harness (exit status 1),
never appear as a time. Run directly or through CTest (bench_harness_selftest)."""

import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
HARNESS = HERE.parent / "run_benchmarks.py"
sys.path.insert(0, str(HERE.parent))
import run_benchmarks as rb  # noqa: E402


def fake_protost(directory, body):
    path = Path(directory) / "protost"
    path.write_text("#!/bin/sh\n"
                    "if [ \"$1\" = \"--version\" ]; then echo 'protoST fake'; exit 0; fi\n"
                    + body + "\n")
    path.chmod(path.stat().st_mode | stat.S_IEXEC)
    return path


def run_harness(binary):
    env = {**os.environ, "PROTOST_BIN": str(binary)}
    return subprocess.run([sys.executable, str(HARNESS), "--no-python", "--skip-actors",
                           "--runs", "1", "--warmup", "0"],
                          env=env, capture_output=True, text=True, timeout=120)


def main():
    # The extractors.
    assert rb.verified_value("noise\nVERIFIED 42\n") == "42"
    assert rb.verified_value("VERIFIED 42\nmore output\n") is None
    assert rb.verified_value("") is None
    assert rb.bench_result_value("BENCH_RESULT name=x N=3 result=7 ms=1.0") == "7"
    assert rb.bench_result_value("nothing") is None

    with tempfile.TemporaryDirectory() as d:
        for label, body in [("wrong value", "echo 'VERIFIED 41'"),
                            ("no VERIFIED line", "echo 'done'"),
                            ("crash", "exit 3"),
                            ("silent success", "exit 0")]:
            r = run_harness(fake_protost(d, body))
            assert r.returncode == 1, f"{label}: harness exit {r.returncode}\n{r.stdout}\n{r.stderr}"
            assert "VERIFICATION FAILURES" in r.stdout, f"{label}: no failure report\n{r.stdout}"
    print("bench harness self-test: PASS")


if __name__ == "__main__":
    main()
