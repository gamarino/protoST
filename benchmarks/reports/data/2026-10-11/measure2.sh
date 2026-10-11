#!/usr/bin/env bash
set -uo pipefail
B=$SCRATCH
ST=$PROJECTS/protoST
OLD=$B/st050-build/protost; MID=/usr/bin/protost; NEW=$ST/build_bench/protost
eval "$(sed -n '/^busy()/,/^}/p' $B/measure.sh)"
DEADLINE=$(( $(date +%s) + 3*3600 ))
eval "$(sed -n '/^wait_quiet()/,/^}/p' $B/measure.sh)"
cd $ST
wait_quiet
python3 benchmarks/compare_builds.py --json $B/compare-r3.json "0.5.0/2.6.2=$OLD" "oct03/2.10.2=$MID" "main/2.14.5=$NEW" > $B/compare-r3.txt 2>&1; echo "compare exit $?"
for rnd in 1 2; do
  wait_quiet
  for pair in "050=$OLD" "oct03=$MID" "main=$NEW"; do
    l=${pair%%=*}; bin=${pair#*=}
    for w in startup fib intsum; do
      case $w in startup) a="-e 1";; fib) a=benchmarks/comparable/fib.st;; intsum) a=benchmarks/comparable/int_sum_loop.st;; esac
      echo "busy=[$(busy)]" > $B/perf-$w-$l-r$rnd.csv
      LC_ALL=C perf stat -r 3 -x, -e task-clock,cycles,instructions --append -o $B/perf-$w-$l-r$rnd.csv $bin $a > /dev/null
    done
  done
done
echo "busy at end: $(busy)"
echo MEASURE2_DONE
