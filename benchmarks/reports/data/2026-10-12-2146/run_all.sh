#!/usr/bin/env bash
# The whole session, in order. The same /usr/bin/protost (the protoST 0.6.4
# release package, GCC 13) on both sides; LD_LIBRARY_PATH selects the
# libprotoCore.so.3 extracted from the GitHub release .deb of protoCore 2.14.5
# or 2.14.6 (checked with ldd before every run). Run from the repository root.
# Usage: run_all.sh LIB2145 LIB2146
set -u
L5="$1"; L6="$2"
D=benchmarks/reports/data/2026-10-12-2146
R=$D/results; mkdir -p $R
ldd /usr/bin/protost > $R/ldd-system.txt
LD_LIBRARY_PATH=$L5 ldd /usr/bin/protost > $R/ldd-2145.txt
LD_LIBRARY_PATH=$L6 ldd /usr/bin/protost > $R/ldd-2146.txt
sha256sum "$L5/libprotoCore.so.2.14.5" "$L6/libprotoCore.so.2.14.6" /usr/bin/protost > $R/sha256.txt
echo "== compare_builds $(date +%T)"
python3 benchmarks/compare_builds.py --json $R/compare.json --runs 7 --warmup 1 \
  2145a=/usr/bin/protost 2146a=/usr/bin/protost 2145b=/usr/bin/protost 2146b=/usr/bin/protost \
  --libdir 2145a=$L5 --libdir 2145b=$L5 --libdir 2146a=$L6 --libdir 2146b=$L6 2>&1 | tee $R/compare.txt
echo "== demo 2 $(date +%T)"
python3 $D/demo2.py --json $R/demo2.json --runs 15 2.14.5=$L5 2.14.6=$L6 2>&1 | tee $R/demo2.txt
echo "== perf stat $(date +%T)"
python3 $D/perfstat.py --json $R/perfstat.json --repeat 5 \
  2.14.5=/usr/bin/protost@$L5 2.14.6=/usr/bin/protost@$L6 2>&1 | tee $R/perfstat.txt
echo "== K5 $(date +%T)"
$D/k5.sh $L5 $L6 5 2>&1 | tee $R/k5.txt
for side in "2.14.6 $L6" "2.14.5 $L5"; do
  label=${side%% *}; lib=${side#* }
  echo "== CPython vs protoST on $label $(date +%T)"
  LD_LIBRARY_PATH=$lib PROTOST_BIN=/usr/bin/protost PROTOPY_BIN=/nonexistent \
    python3 benchmarks/run_benchmarks.py --runs 7 --warmup 1 --skip-actors \
    --output $R/cpython-$label.md 2>&1 | tee $R/cpython-$label.txt
done
echo "== demos 1-4, 10 runs each, installed 0.6.4 copy, on 2.14.6 $(date +%T)"
(cd /usr/share/protoST/talks/2026-10-15-fas/demos && LD_LIBRARY_PATH=$L6 RUNS=10 ./check_all.sh) 2>&1 | tee $R/demos-2146.txt
echo "== done $(date +%T)"
