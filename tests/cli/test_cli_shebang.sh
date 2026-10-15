#!/usr/bin/env bash
# A script whose first line is `#!/usr/bin/env protost` runs as a command:
# the interpreter line is skipped, diagnostics keep the file's real line
# numbers, and such scripts chain in a pipeline with each other and with the
# Unix tools (examples/unix/).
set -u
PROTOST="$1"
SRC="$(cd "$(dirname "$0")/../.." && pwd)"
# The interpreter line itself is honoured by the kernel, which Windows does
# not have; there only the "protost script" half below is checked.
D=$(mktemp -d); trap 'rm -rf "$D"' EXIT

# Run through protost: the line is skipped and line numbers are the file's.
printf '#!/usr/bin/env protost\n(3 + 4) printNl.\nnil qux.\n' > "$D/s.st"
out=$("$PROTOST" "$D/s.st" 2>&1); rc=$?
[[ $rc -eq 1 && "$out" == "7"* && "$out" == *"doesNotUnderstand: qux"* && "$out" == *"s.st:3"* ]] \
    || { echo "FAIL (protost script, rc=$rc): $out"; exit 1; }
out=$(printf '#!/usr/bin/env protost\n2 * 21.\n' | "$PROTOST" --print-last - 2>&1)
[[ "$out" == "42" ]] || { echo "FAIL (stdin): $out"; exit 1; }

command -v cygpath >/dev/null 2>&1 && { echo OK; exit 0; }

# Run as commands: `env` must find this build's protost first on PATH.
mkdir "$D/bin"; ln -s "$PROTOST" "$D/bin/protost"
export PATH="$D/bin:$PATH"
cp "$SRC/examples/unix/column.st" "$SRC/examples/unix/sum.st" "$D/"
chmod +x "$D/column.st" "$D/sum.st"
cd "$D"
out=$(printf 'pump-1 12\npump-2 30\n\nshort\n' | ./column.st 2 | ./sum.st 2>&1)
[[ "$out" == "42" ]] || { echo "FAIL (column | sum): $out"; exit 1; }
out=$(seq 1 100 | ./sum.st 2>&1)
[[ "$out" == "5050" ]] || { echo "FAIL (seq | sum): $out"; exit 1; }
# Exit status reaches the shell: 1 for bad input, 2 for a usage error.
out=$(printf '1\nx\n' | ./sum.st 2>&1); rc=$?
[[ $rc -eq 1 && "$out" == *"line 2 is not a number: x"* ]] || { echo "FAIL (sum rc=$rc): $out"; exit 1; }
out=$(./column.st </dev/null 2>&1); rc=$?
[[ $rc -eq 2 && "$out" == "usage: column.st N"* ]] || { echo "FAIL (column usage rc=$rc): $out"; exit 1; }
# A filter stops when its reader goes away (head), instead of running on.
out=$(seq 1 200000 | sed 's/^/p /' | ./column.st 2 | head -n 2 | tr '\n' ' ')
[[ "$out" == "1 2 " ]] || { echo "FAIL (| head): $out"; exit 1; }
echo OK
