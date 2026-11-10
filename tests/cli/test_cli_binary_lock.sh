#!/usr/bin/env bash
# BinaryFile's lock across processes (docs/superpowers/specs/
# 2026-11-09-bytes-files-timers-design.md, section 4.4).
#
# Usage: test_cli_binary_lock.sh <protost> protost|protobase [<python> <ownerlock_peer.py>]
#
#   protost    one protoST process holds `lock` while a second answers
#              `tryLock = false` and blocks in `lock` until the first releases.
#   protobase  ProtoBase's owner lock (protoIO's tests/ownerlock_peer.py, which
#              takes it exactly as proto_db/storage/ownerlock.py does) excludes
#              protoST, and protoST's lock excludes it. <python> is the
#              interpreter CMake found (Python3_EXECUTABLE), or `none`. Without
#              it or the peer script it prints a SKIP line and exits 77, which
#              CTest reports as skipped: never a silent pass.
set -euo pipefail
PROTOST="$1"
MODE="$2"
PYTHON="${3:-}"
PEER="${4:-}"
dir=$(mktemp -d "${TMPDIR:-/tmp}/protost-binlock-XXXXXX")
holder=""
cleanup() {
    # A holder or a peer still waiting is released, then every background
    # job is reaped, so none outlives the test holding its output open.
    : > "$dir/stop" 2>/dev/null || true
    : > "$dir/peer-stop" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$dir"
}
trap cleanup EXIT

# Waits up to 10 s for a file to appear.
await() {
    for _ in $(seq 1 200); do
        [[ -e "$1" ]] && return 0
        sleep 0.05
    done
    echo "FAIL: $2 (no $1 within 10 s)"
    exit 1
}

lock="$dir/owner.lock"

# Answers the tryLock of a fresh protoST process on the lock file.
cat > "$dir/try.st" <<'ST'
| f got |
f := BinaryFile open: (Smalltalk arguments at: 1) options: #(#read #write #create).
got := f tryLock.
f close.
Transcript showCr: got printString.
ST

# Holds the lock until a line arrives on its standard input. It creates
# <ready> once it holds the lock and <releasing> just before it unlocks.
cat > "$dir/holder.st" <<'ST'
| f |
f := BinaryFile open: (Smalltalk arguments at: 1) options: #(#read #write #create).
f lock.
(Smalltalk arguments at: 2) asFileReference binaryContents: #[].
Stdio stdin nextLine.
(Smalltalk arguments at: 3) asFileReference binaryContents: #[].
f unlock.
f close.
Transcript showCr: 'released'.
ST

# Starts holder.st in the background; its stdin is a pipe that receives a
# line once $dir/stop exists (at most 30 s later).
start_holder() {
    ( for _ in $(seq 1 600); do [[ -e "$dir/stop" ]] && break; sleep 0.05; done; echo stop ) \
        | "$PROTOST" "$dir/holder.st" "$lock" "$dir/ready" "$dir/releasing" > "$dir/holder.out" 2>&1 &
    holder=$!
    await "$dir/ready" "the holder did not take the lock"
}

stop_holder() {
    : > "$dir/stop"
    wait "$holder" || { echo "FAIL: the holder failed: $(cat "$dir/holder.out")"; exit 1; }
    holder=""
    rm -f "$dir/stop" "$dir/ready" "$dir/releasing"
}

case "$MODE" in
protost)
    # (a) tryLock from a second process is refused while the first holds it.
    start_holder
    out=$("$PROTOST" "$dir/try.st" "$lock" 2>&1 || true)
    [[ "$out" == "false" ]] || { echo "FAIL: tryLock while another process holds the lock answered: $out"; exit 1; }
    stop_holder
    out=$("$PROTOST" "$dir/try.st" "$lock" 2>&1 || true)
    [[ "$out" == "true" ]] || { echo "FAIL: tryLock after the holder released answered: $out"; exit 1; }

    # lock from a second process waits until the first releases: when it
    # returns, the holder has already created <releasing>.
    cat > "$dir/waiter.st" <<'ST'
| f |
f := BinaryFile open: (Smalltalk arguments at: 1) options: #(#read #write #create).
(Smalltalk arguments at: 2) asFileReference binaryContents: #[].
f lock.
Transcript showCr: (Smalltalk arguments at: 3) asFileReference exists printString.
f unlock.
f close.
ST
    start_holder
    "$PROTOST" "$dir/waiter.st" "$lock" "$dir/waiting" "$dir/releasing" > "$dir/waiter.out" 2>&1 &
    waiter=$!
    await "$dir/waiting" "the waiter did not start"
    # The sleep only strengthens the check and never decides the result: a
    # correct lock prints true however long the waiter takes to reach flock
    # (the holder creates <releasing> before it unlocks), and a lock that
    # did not wait would print false whenever it got past flock before the
    # release; the pause just makes that early pass very likely to happen.
    sleep 0.3
    : > "$dir/stop"
    wait "$waiter" || { echo "FAIL: the waiter failed: $(cat "$dir/waiter.out")"; exit 1; }
    out=$(cat "$dir/waiter.out")
    [[ "$out" == "true" ]] || { echo "FAIL: lock returned before the holder released (saw: $out)"; exit 1; }
    stop_holder
    echo "PASS: the lock excludes a second protoST process"
    ;;
protobase)
    if [[ -z "$PYTHON" || "$PYTHON" == none ]] || ! "$PYTHON" -c "import sys" > /dev/null 2>&1; then
        echo "SKIP: no usable Python 3 interpreter from CMake (${PYTHON:-none given}), ProtoBase lock interop NOT verified"
        exit 77
    fi
    if [[ -z "$PEER" || ! -f "$PEER" ]]; then
        echo "SKIP: protoIO's ownerlock_peer.py not found (${PEER:-no path given}), ProtoBase lock interop NOT verified"
        exit 77
    fi
    # (b) ProtoBase holds its owner lock: protoST is refused, then gets it
    # once the peer releases.
    "$PYTHON" "$PEER" "$lock" "$dir/peer-ready" "$dir/peer-stop" &
    peer=$!
    await "$dir/peer-ready" "the ProtoBase peer did not take the lock"
    out=$("$PROTOST" "$dir/try.st" "$lock" 2>&1 || true)
    [[ "$out" == "false" ]] || { echo "FAIL: protoST's tryLock is not excluded by ProtoBase's OwnerLock (answered $out)"; exit 1; }
    : > "$dir/peer-stop"
    set +e; wait "$peer"; rc=$?; set -e
    [[ $rc -eq 0 ]] || { echo "FAIL: the ProtoBase peer exited $rc"; exit 1; }
    out=$("$PROTOST" "$dir/try.st" "$lock" 2>&1 || true)
    [[ "$out" == "true" ]] || { echo "FAIL: tryLock after the ProtoBase peer released answered: $out"; exit 1; }

    # The reverse: while protoST holds the lock, ProtoBase cannot take it (the
    # peer exits 2 when it is busy; <peer-stop> exists up front, so a peer
    # that wrongly got the lock would release it and exit 0 at once).
    rm -f "$dir/peer-ready"
    : > "$dir/peer-stop"
    start_holder
    set +e; "$PYTHON" "$PEER" "$lock" "$dir/peer-ready" "$dir/peer-stop"; rc=$?; set -e
    [[ $rc -eq 2 && ! -e "$dir/peer-ready" ]] || { echo "FAIL: ProtoBase's OwnerLock was not excluded by protoST's lock (peer exit $rc)"; exit 1; }
    stop_holder
    set +e; "$PYTHON" "$PEER" "$lock" "$dir/peer-ready" "$dir/peer-stop"; rc=$?; set -e
    [[ $rc -eq 0 ]] || { echo "FAIL: ProtoBase could not take the lock after protoST released it (peer exit $rc)"; exit 1; }
    echo "PASS: the lock and ProtoBase's OwnerLock exclude each other"
    ;;
*)
    echo "usage: $0 <protost> protost|protobase [<python> <ownerlock_peer.py>]"
    exit 2
    ;;
esac
