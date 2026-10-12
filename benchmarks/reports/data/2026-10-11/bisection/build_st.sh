#!/usr/bin/env bash
set -uo pipefail
B=/home/gamarino/Documentos/proyectos/.agent_scratch/protost-regression-oct2026
P=/home/gamarino/Documentos/proyectos; ST=$P/.agent_scratch/installers-oct2026/stage2145
for c in "$@"; do
  d=$B/st-$c; [ -x $d/build/protost ] && { echo "$c exists"; continue; }
  rm -rf $d; mkdir -p $d/src; git -C $P/protoST archive $c | tar -x -C $d/src
  (cmake -S $d/src -B $d/build -DCMAKE_BUILD_TYPE=Release -DPROTOCORE_REQUIRE_PACKAGE=ON -DprotoCore_DIR=$ST/usr/lib/x86_64-linux-gnu/cmake/protoCore -DPROTO_CORE_PREFIX=$ST/usr -DprotoIO_DIR=$P/protoIO/build_pkg_oct11 && cmake --build $d/build -j4 --target protost) > $d/build.log 2>&1
  echo "$c rc=$?"
done
