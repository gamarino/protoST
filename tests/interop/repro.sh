#!/usr/bin/env bash
#
# Two-runtime reproducer: protoST and protoScala in one process.
#
# Not registered with CTest. It needs a protoScala build tree beside this one, and it
# documents limits rather than guarding a passing behaviour. See README.md in this
# directory for the output measured on 2026-09-29 and what each line means.
#
# Every compilation is a single, serial compiler invocation.
#
# Usage: repro.sh [work-dir]
# Environment (defaults assume the sibling checkouts of the protoCore workspace):
#   PROTOST_BUILD   protoST build directory holding libprotost_runtime.a and
#                   libprotost_frontend.a                (default: <repo>/build_release)
#   PROTOSCALA_DIR  protoScala source tree with a build_release/ inside
#                                                        (default: <repo>/../protoScala)
#   PROTOCORE_DIR   protoCore source tree with a build_release/ inside
#                                                        (default: <repo>/../protoCore)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
PROTOST_BUILD="${PROTOST_BUILD:-$REPO/build_release}"
PS="${PROTOSCALA_DIR:-$REPO/../protoScala}"
PC="${PROTOCORE_DIR:-$REPO/../protoCore}"
WORK="${1:-$(mktemp -d)}"
mkdir -p "$WORK/so"
cd "$WORK" || exit 1

for f in "$PROTOST_BUILD/libprotost_runtime.a" "$PROTOST_BUILD/libprotost_frontend.a" \
         "$PS/build_release/libprotoScala.so" "$PS/build_release/protoscalac" \
         "$PC/build_release/libprotoCore.so"; do
    [[ -e "$f" ]] || { echo "missing prerequisite: $f"; exit 2; }
done

cp "$HERE"/counter_lib.st "$HERE"/import_scala.st "$HERE"/send_to_scala.st \
   "$HERE"/mathlib.scala .

# 1. The protoScala module, transpiled and compiled to a shared object. Without
#    --module-name so that protoscalac exports the top-level defs (on 2026-09-29,
#    `--as-module --module-name mathlib` produced a module with zero exports).
echo "== protoscalac mathlib.scala --build-so"
"$PS/build_release/protoscalac" mathlib.scala -o so --build-so >so/build.out 2>&1 \
    || { cat so/build.out; exit 1; }
mv so/module.so so/mathlib.so

# 2. The probe host. protoScala is linked as its SHARED library so that the host and
#    the generated module share one copy of protoScala's runtime state; linking
#    protoScala's object files statically gives the process two copies, and the
#    module's initialiser then reports "no protoScala call context is active".
echo "== c++ two_runtime_probe.cpp"
nice c++ -O1 -std=c++20 -DPROTOSCALA_HAS_PMQ=1 -DPROTOSCALA_HAVE_PRELUDE_IMAGE \
    -I"$PC" -I"$PC/headers" -I"$PS/src" -I"$PS/include" -I"$PS/build_release/generated" \
    -I"$REPO/include" \
    "$HERE/two_runtime_probe.cpp" -o two_runtime_probe \
    -Wl,-rpath,"$PS/build_release" -Wl,-rpath,"$PC/build_release" \
    "$PROTOST_BUILD/libprotost_runtime.a" "$PROTOST_BUILD/libprotost_frontend.a" \
    -L"$PS/build_release" -lprotoScala "$PC/build_release/libprotoCore.so" \
    -ldl -lpthread -lreadline || exit 1

export PROTOST_LIB="$REPO/lib"
export STPATH="$WORK"
export PROTOSCALA_MODULE_PATH="$WORK/so"

# 3a. protoST imports the INTERPRETED module: only mathlib.scala is visible.
echo "== import, interpreted provider (mathlib.scala, no .so on the path)"
PROTOSCALA_MODULE_PATH="$WORK/none" timeout 60 ./two_runtime_probe import import_scala.st
echo "exit=$?"

# 3b. protoST imports the COMPILED module: only so/mathlib.so is visible.
echo "== import, compiled provider (so/mathlib.so, no .scala beside it)"
mv mathlib.scala mathlib.scala.hidden
timeout 60 ./two_runtime_probe import import_scala.st
echo "exit=$?"
mv mathlib.scala.hidden mathlib.scala

# 4. No UMD: protoScala loads the module in its own space and the host hands the
#    object to protoST.
echo "== handoff"
timeout 60 ./two_runtime_probe handoff "$WORK/so/mathlib.so" send_to_scala.st
echo "exit=$?"
