#!/bin/bash
# protoST BEFORE (c0ba168 on protoCore cf52f9f5) vs AFTER (b8b71b3 on protoCore
# 65760e4a), Release, clean worktrees. Each run behind the quiet gate
# (QUIET_CORES=4.0) and on a fresh copy of the variant's libprotoCore.
set -u
export QUIET_CORES=${QUIET_CORES:-4.0}
WS=/home/gamarino/Documentos/proyectos
M=$WS/.agent_scratch/meas_1022; OUT=$M/st_out; mkdir -p $OUT/libs
Q="python3 $WS/protoCore/docs/reports/data/2026-10-12-mark-stack-int-compare/quiet.py"
CP=/usr/local/bin/python3.14
bin() { echo $M/$1/protoST/build_release/protost; }
lib() { echo $M/$1/protoCore/build_release/libprotoCore.so.4; }
fresh() { local d=$OUT/libs/$2; mkdir -p $d; cp -L $(lib $1) $d/libprotoCore.so.4; export LD_LIBRARY_PATH=$d
          echo "== $2" >> $OUT/ldd.txt; ldd $(bin $1) | grep -iE "proto|ssl" >> $OUT/ldd.txt; }
# 1. self-report verification: every comparable, actor benchmark and probe, once per variant
for v in before after; do
  fresh $v verify_$v
  for f in $M/after/protoST/benchmarks/comparable/*.st $M/after/protoST/benchmarks/actors/{parallel_speedup,cooperative_yield,message_throughput,saturation_big}.st $M/st_probes/*.st; do
    o=$(timeout 600 $(bin $v) $f 2>&1); rc=$?
    echo "$v $(basename $f) rc=$rc last=$(echo "$o" | grep -v '^\s*$' | tail -n 1)" >> $OUT/verify.txt
  done
done
unset LD_LIBRARY_PATH
# 2. the harness (benchmarks/run_benchmarks.py, AFTER's copy: benchmarks/ is identical
#    in both commits), three rounds, order alternated per round
for r in 1 2 3; do
  order="after before"; [ $r = 2 ] && order="before after"
  for v in $order; do
    $Q > $OUT/quiet_${v}_r$r.txt 2>&1; fresh $v ${v}_r$r; echo "$(date -u +%FT%TZ) st $v r$r"
    PROTOST_BIN=$(bin $v) CPYTHON_BIN=$CP PROTOPY_BIN=/nonexistent \
      python3 $M/after/protoST/benchmarks/run_benchmarks.py --output $OUT/st_${v}_r$r.md > $OUT/st_${v}_r$r.log 2>&1
    echo "rc=$?" >> $OUT/st_${v}_r$r.log
  done
done
# 3. probes, perf stat -r 3 cycles, BEFORE/AFTER interleaved per probe
$Q > $OUT/quiet_probes.txt 2>&1; fresh before probes_before; LB=$LD_LIBRARY_PATH; fresh after probes_after; LA=$LD_LIBRARY_PATH; unset LD_LIBRARY_PATH
echo "$(date -u +%FT%TZ) probes"
for f in $M/st_probes/*.st; do
  for v in before after; do
    L=$LB; [ $v = after ] && L=$LA
    c="env LD_LIBRARY_PATH=$L $(bin $v) $f"
    chk=$($c 2>&1 | grep CHECK | tail -n 1)
    o=$(perf stat -r 3 -x, -e cycles,task-clock $c 2>&1 >/dev/null | grep -E 'cycles|task-clock' | cut -d, -f1 | paste -sd' ')
    echo "$(basename $f .st) $v $o | $chk" >> $OUT/probes.txt
  done
done
echo DONE
