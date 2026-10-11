#!/usr/bin/env bash
set -uo pipefail
B=$SCRATCH
ST=$PROJECTS/protoST
eval "$(sed -n '/^busy()/,/^}/p' $B/measure.sh)"
DEADLINE=$(( $(date +%s) + 3*3600 ))
eval "$(sed -n '/^wait_quiet()/,/^}/p' $B/measure.sh)"
cd $ST
wait_quiet
python3 benchmarks/compare_builds.py --json $B/diag.json --only "fib,parallel_speedup pool,parallel_speedup w=1,cooperative_yield w=2,saturation_big w=1,saturation_big w=4" \
  "0.5.0/2.6.2=$B/st050-build/protost" "main=$ST/build_bench/protost" "main,no-pacing=$B/main-nopacing" "main,1-cursor=$B/main-1cursor" "main,no-ivar-groups=$B/main-noivg" > $B/diag.txt 2>&1; echo "diag exit $?"
echo MEASURE3_DONE
