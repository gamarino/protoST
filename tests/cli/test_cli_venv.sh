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
# One activation script per shell, on every platform: POSIX sh/bash/zsh,
# fish, PowerShell and cmd.exe (with its deactivate.bat). Each names the
# venv; the templates are built into protost, so an installed copy writes
# them as well as one run from the build tree.
for f in activate activate.fish Activate.ps1 activate.bat deactivate.bat; do
    [ -s ".venv/bin/$f" ]            || { echo "FAIL: $f missing or empty"; exit 1; }
done
for f in activate activate.fish Activate.ps1 activate.bat; do
    grep -qF ".venv" ".venv/bin/$f"  || { echo "FAIL: $f does not name the venv"; cat ".venv/bin/$f"; exit 1; }
done

# The venv's path as protost prints it. Under Git for Windows' bash $tmp is a
# POSIX path; a native protost sees and prints the Windows form instead: with
# backslashes when it finds the venv itself, with forward slashes when it is
# handed the path that bash converted.
venv="$tmp/.venv"; venv_found="$venv"
if command -v cygpath >/dev/null 2>&1; then
    # -l: the long form (C:\Users\runneradmin, not C:\Users\RUNNER~1), which
    # is the one protost prints.
    venv="$(cygpath -m -l "$tmp")/.venv"; venv_found="$(cygpath -w -l "$tmp")\.venv"
fi

# venv info from inside the project (no STENV) should discover the venv
out=$("$PROTOST" venv info)
grep -qF "$venv_found" <<< "$out" || { echo "FAIL: venv info did not discover"; echo "$out"; exit 1; }

# explicit STENV overrides discovery. It is passed in the form it is compared
# in: protost prints STENV as given, and Git for Windows' bash would convert
# "$tmp/.venv" to the short 8.3 form (C:/Users/RUNNER~1/...) of the temp dir.
out=$(STENV="$venv" "$PROTOST" venv info) || { echo "FAIL: STENV venv info exited non-zero"; exit 1; }
grep -qF "$venv" <<< "$out" || { echo "FAIL: STENV override"; echo "$out"; exit 1; }

# Activation sets STENV and puts the venv's bin first on PATH. POSIX shells
# source `activate`; on Windows cmd.exe calls `activate.bat`, and
# `deactivate.bat` undoes it.
if command -v cygpath >/dev/null 2>&1; then
    bat=$(cygpath -w "$tmp/.venv/bin")
    out=$(MSYS_NO_PATHCONV=1 cmd.exe /d /c "call $bat\activate.bat && set STENV && path && call $bat\deactivate.bat && set STENV" 2>&1 | tr -d '\r')
    grep -qxF "STENV=$venv_found" <<< "$out" || { echo "FAIL: activate.bat STENV"; echo "$out"; exit 1; }
    grep -qiF "PATH=$venv_found\bin;" <<< "$out" || { echo "FAIL: activate.bat PATH"; echo "$out"; exit 1; }
    [[ "$(grep -c '^STENV=' <<< "$out")" == 1 ]] || { echo "FAIL: deactivate.bat left STENV set"; echo "$out"; exit 1; }
else
    out=$(unset STENV; . .venv/bin/activate && echo "STENV=$STENV" && echo "PATH=$PATH" && deactivate && echo "after=${STENV:-unset}")
    grep -qxF "STENV=$venv" <<< "$out" || { echo "FAIL: activate STENV"; echo "$out"; exit 1; }
    grep -qF "PATH=$venv/bin:" <<< "$out" || { echo "FAIL: activate PATH"; echo "$out"; exit 1; }
    grep -qxF "after=unset" <<< "$out" || { echo "FAIL: deactivate"; echo "$out"; exit 1; }
fi

echo OK
