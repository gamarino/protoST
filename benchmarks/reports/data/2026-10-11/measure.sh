#!/usr/bin/env bash
# Waits for a quiet machine, then runs the interleaved comparison, the harness
# and perf stat. Re-runs once if a competitor showed up during the comparison.
set -uo pipefail
B=$SCRATCH
ST=$PROJECTS/protoST
OLD=$B/st050-build/protost; MID=/usr/bin/protost; NEW=$ST/build_bench/protost
DEADLINE=$(( $(date +%s) + 3*3600 ))
busy() {
  local b; b="$(ps -eo comm= | grep -E '^(cc1plus|cc1|ctest|test262|node|lto1|ld|make|gmake|ninja)$|^proto' | sort | uniq -c | tr '\n' ' ')"
  local l; l=$(cut -d' ' -f1 /proc/loadavg)
  if [ -n "$b" ] || awk "BEGIN{exit !($l >= 5.0)}"; then echo "${b}load1=$l"; fi
}
wait_quiet() {
  while :; do
    b="$(busy)"
    if [ -z "$b" ]; then echo "$(date +%T) quiet; load $(cut -d' ' -f1-3 /proc/loadavg)"; return 0; fi
    if [ "$(date +%s)" -ge "$DEADLINE" ]; then echo "$(date +%T) DEADLINE reached; measuring under load: $b"; return 1; fi
    echo "$(date +%T) busy: $b; load $(cut -d' ' -f1-3 /proc/loadavg)"; sleep 60
  done
}
cd $ST
for attempt in 1 2 3; do
  wait_quiet; q=$?
  python3 benchmarks/compare_builds.py --json $B/compare-$attempt.json \
    "0.5.0/2.6.2=$OLD" "oct03/2.10.2=$MID" "main/2.14.5=$NEW" > $B/compare-$attempt.txt 2>&1
  echo "compare attempt $attempt exit $?"
  contaminated=$(python3 -c "
import json; d=json.load(open('$B/compare-$attempt.json'))
print(sum(1 for w in d['workloads'] for v in w['samples'].values() for s in v if s['contenders']))")
  echo "contaminated samples: $contaminated"
  [ "$contaminated" -eq 0 ] && break
  [ "$q" -ne 0 ] && break
done
wait_quiet
PROTOST_BIN=$NEW PROTOPY_BIN=/nonexistent python3 benchmarks/run_benchmarks.py --runs 3 --warmup 1 \
  --output $B/harness-main.md > $B/harness-main.txt 2>&1; echo "harness exit $?"
PROTOST_BIN=$OLD PROTOPY_BIN=/nonexistent python3 benchmarks/run_benchmarks.py --runs 3 --warmup 1 --skip-actors \
  --output $B/harness-050.md > $B/harness-050.txt 2>&1; echo "harness 050 exit $?"
wait_quiet
for pair in "050=$OLD" "oct03=$MID" "main=$NEW"; do
  l=${pair%%=*}; bin=${pair#*=}
  perf stat -r 3 -x, -e task-clock,cycles,instructions -o $B/perf-startup-$l.csv $bin -e 1 > /dev/null
  perf stat -r 3 -x, -e task-clock,cycles,instructions -o $B/perf-fib-$l.csv $bin benchmarks/comparable/fib.st > $B/perf-fib-$l.out
  perf stat -r 3 -x, -e task-clock,cycles,instructions -o $B/perf-intsum-$l.csv $bin benchmarks/comparable/int_sum_loop.st > $B/perf-intsum-$l.out
done
python3 $B/tutorial_times.py > $B/tutorial.txt 2>&1; echo "tutorial exit $?"
echo "busy at end: $(busy); load $(cut -d' ' -f1-3 /proc/loadavg)"
echo MEASURE_DONE
