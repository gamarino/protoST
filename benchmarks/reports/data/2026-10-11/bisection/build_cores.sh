#!/usr/bin/env bash
# Builds libprotoCore (Release, default options) for each tag, from git archive.
set -uo pipefail
B=/home/gamarino/Documentos/proyectos/.agent_scratch/protost-regression-oct2026
PC=/home/gamarino/Documentos/proyectos/protoCore
SUF=${SUF:-}
for t in "$@"; do
  v=${t#v}; d=$B/core-$v$SUF
  [ -f $d/build/libprotoCore.so.$v ] && { echo "$v$SUF exists"; continue; }
  rm -rf $d; mkdir -p $d/src
  git -C $PC archive $t | tar -x -C $d/src
  cmake -S $d/src -B $d/build -DCMAKE_BUILD_TYPE=Release ${EXTRA:-} > $d/build.log 2>&1 \
    && cmake --build $d/build -j4 --target protoCore >> $d/build.log 2>&1
  echo "$v$SUF rc=$?"
done
