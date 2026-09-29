#!/usr/bin/env python3
"""Self-test for run_doc_snippets.py.

Usage: test_run_doc_snippets.py <protost-binary> [--lib DIR]

Writes a tiny synthetic Markdown document with exactly one passing and one
failing example (plus a `no-run` block that would fail if it were run and an
untagged-result block that must only be counted as unchecked), runs the tool
on it, and checks that the tool reports exactly one failure, at the expected
line, and exits non-zero. Exit status 0 means the self-test passed.
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOL = HERE / "run_doc_snippets.py"

DOC = """\
# Synthetic document

A passing inline annotation:

```smalltalk
3 + 4        "=> 7"
```

A failing shell transcript (the stated output is deliberately wrong):

```bash
$ ./build/protost -e '6 * 7'
41
```

A block tagged no-run is never executed, even though it would fail:

```smalltalk no-run
1 + 1        "=> 3"
```

A block with no stated result is unchecked, not failed:

```smalltalk
x := 5.
```
"""

FAIL_LINE = 12   # the `$ ./build/protost -e '6 * 7'` line


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    extra = argv[1:]
    with tempfile.TemporaryDirectory(prefix="protost-docs-selftest-") as tmp:
        doc = Path(tmp) / "synthetic.md"
        doc.write_text(DOC, encoding="utf-8")
        p = subprocess.run([sys.executable, str(TOOL), *extra, "-v", argv[0], str(doc)],
                           capture_output=True, text=True, timeout=120)
    out = p.stdout
    print(out)
    if p.stderr:
        print(p.stderr, file=sys.stderr)

    problems = []
    fails = re.findall(r"^FAIL \S+:(\d+)", out, re.M)
    passes = re.findall(r"^PASS \S+:(\d+)", out, re.M)
    if p.returncode != 1:
        problems.append(f"expected exit status 1, got {p.returncode}")
    if fails != [str(FAIL_LINE)]:
        problems.append(f"expected exactly one FAIL at line {FAIL_LINE}, got {fails}")
    if len(passes) != 1:
        problems.append(f"expected exactly one PASS, got {passes}")
    summary = re.search(r"^\S*synthetic\.md\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$", out, re.M)
    if not summary:
        problems.append("per-file summary row not found")
    elif summary.groups() != ("2", "1", "1", "1", "1"):
        problems.append("expected summary examples=2 passed=1 failed=1 unchecked=1 "
                        f"skipped=1, got {summary.groups()}")
    if problems:
        print("SELF-TEST FAILED:\n  " + "\n  ".join(problems))
        return 1
    print("SELF-TEST PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
