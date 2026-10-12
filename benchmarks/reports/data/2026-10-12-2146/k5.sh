#!/usr/bin/env bash
# K5: `(1 to: 500000) asArray` (k5.st) under the fixed 640 MB limit
# (PROTOCORE_ADAPTIVE_HEAP=0; expected exit 3, out of memory) and under the
# default controller (expected to complete and print 500000), RUNS times per
# side, sides alternating. Peak RSS and wall from GNU time.
# Usage: k5.sh LIB2145 LIB2146 [RUNS]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
RUNS="${3:-5}"
for i in $(seq 1 "$RUNS"); do
  for side in "2.14.5 $1" "2.14.6 $2"; do
    label=${side%% *}; lib=${side#* }
    core=$(LD_LIBRARY_PATH=$lib ldd /usr/bin/protost | grep -o "$lib[^ ]*")
    [ -n "$core" ] || { echo "ldd mismatch for $label"; exit 1; }
    for mode in fixed controller; do
      if [ $mode = fixed ]; then envs="PROTOCORE_ADAPTIVE_HEAP=0"; want=3; else envs="PROTOCORE_ADAPTIVE_HEAP=1"; want=0; fi
      out=$(env LD_LIBRARY_PATH=$lib $envs /usr/bin/time -f "TIME %M %e" /usr/bin/protost "$HERE/k5.st" 2>&1); rc=$?
      t=$(echo "$out" | grep '^TIME'); ok=yes
      [ $rc -eq $want ] || ok=NO
      [ $mode = controller ] && ! echo "$out" | grep -qx 500000 && ok=NO
      echo "$label $mode run $i rc $rc ok $ok peak_kb $(echo $t | cut -d' ' -f2) wall_s $(echo $t | cut -d' ' -f3) load $(cut -d' ' -f1 /proc/loadavg)"
    done
  done
done
