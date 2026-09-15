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

# venv info from inside the project (no STENV) should discover the venv
out=$("$PROTOST" venv info)
grep -q "$tmp/.venv" <<< "$out" || { echo "FAIL: venv info did not discover"; echo "$out"; exit 1; }

# explicit STENV overrides discovery
out=$(STENV="$tmp/.venv" "$PROTOST" venv info) || { echo "FAIL: STENV venv info exited non-zero"; exit 1; }
grep -q "$tmp/.venv" <<< "$out" || { echo "FAIL: STENV override"; echo "$out"; exit 1; }

echo OK
