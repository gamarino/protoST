#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
tmp=$(mktemp -d)
trap "rm -rf $tmp" EXIT

cd "$tmp"
"$PROTOST" venv create .venv

[ -d .venv/bin ]                    || { echo "FAIL: bin missing"; exit 1; }
[ -d .venv/lib/protoST/modules ]    || { echo "FAIL: modules dir missing"; exit 1; }
[ -f .venv/stenv.cfg ]              || { echo "FAIL: stenv.cfg missing"; exit 1; }
[ -f .venv/bin/activate ]           || { echo "FAIL: activate missing"; exit 1; }

# The venv's path as protost prints it. Under Git for Windows' bash $tmp is a
# POSIX path; a native protost sees and prints the Windows form instead: with
# backslashes when it finds the venv itself, with forward slashes when it is
# handed the path that bash converted.
venv="$tmp/.venv"; venv_found="$venv"
if command -v cygpath >/dev/null 2>&1; then
    venv="$(cygpath -m "$tmp")/.venv"; venv_found="$(cygpath -w "$tmp")\.venv"
fi

# venv info from inside the project (no STENV) should discover the venv
out=$("$PROTOST" venv info)
grep -qF "$venv_found" <<< "$out" || { echo "FAIL: venv info did not discover"; echo "$out"; exit 1; }

# explicit STENV overrides discovery
out=$(STENV="$tmp/.venv" "$PROTOST" venv info) || { echo "FAIL: STENV venv info exited non-zero"; exit 1; }
grep -qF "$venv" <<< "$out" || { echo "FAIL: STENV override"; echo "$out"; exit 1; }

echo OK
