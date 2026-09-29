#!/usr/bin/env bash
# Standard streams, arguments and exit codes, seen from outside the program.
set -euo pipefail
PROTOST="$1"
dir=$(mktemp -d "${TMPDIR:-/tmp}/protost-io-XXXXXX")
trap 'rm -rf "$dir"' EXIT

# Arguments: the script's own, after its path.
printf "Smalltalk arguments printNl.\n" > "$dir/args.st"
out=$("$PROTOST" "$dir/args.st" one 'two words' 3)
[[ "$out" == "#('one' 'two words' '3')" ]] || { echo "FAIL arguments: $out"; exit 1; }
out=$("$PROTOST" --print-last "$dir/args.st" x)
[[ "${out%%$'\n'*}" == "#('x')" ]] || { echo "FAIL arguments with --print-last: $out"; exit 1; }

# stdin line by line, then to the end.
cat > "$dir/stdin.st" <<'ST'
n := 0. total := 0.
[(line := Stdio stdin nextLine) notNil] whileTrue: [n := n + 1. total := total + line asNumber].
Transcript showCr: n printString , ' ' , total printString.
ST
out=$(printf '10\n20\n12\n' | "$PROTOST" "$dir/stdin.st")
[[ "$out" == "3 42" ]] || { echo "FAIL stdin nextLine: $out"; exit 1; }
printf "Transcript showCr: Stdio stdin upToEnd size printString.\n" > "$dir/all.st"
out=$(printf 'abcdef' | "$PROTOST" "$dir/all.st")
[[ "$out" == "6" ]] || { echo "FAIL stdin upToEnd: $out"; exit 1; }

# stderr is separate from stdout.
printf "Stdio stderr nextPutAll: 'err'; lf. Stdio stdout nextPutAll: 'out'; lf.\n" > "$dir/err.st"
out=$("$PROTOST" "$dir/err.st" 2>/dev/null)
[[ "$out" == "out" ]] || { echo "FAIL stdout: $out"; exit 1; }
err=$("$PROTOST" "$dir/err.st" 2>&1 >/dev/null)
[[ "$err" == "err" ]] || { echo "FAIL stderr: $err"; exit 1; }

# Exit codes, with pending output flushed.
printf "Transcript show: 'bye'. Smalltalk exit: 3.\n'not reached' displayNl.\n" > "$dir/exit.st"
set +e; out=$("$PROTOST" "$dir/exit.st"); rc=$?; set -e
[[ $rc -eq 3 && "$out" == "bye" ]] || { echo "FAIL exit: rc=$rc out=$out"; exit 1; }

# A filter: read stdin, write stdout, as part of a pipeline.
printf "Stdio stdin linesDo: [:l | Stdio stdout nextPutAll: l asUppercase; lf].\n" > "$dir/upper.st"
out=$(printf 'ab\ncd\n' | "$PROTOST" "$dir/upper.st" | tr -d '\n')
[[ "$out" == "ABCD" ]] || { echo "FAIL filter: $out"; exit 1; }

# Concurrent readers of one stream: every line reaches exactly one reader,
# whole (the descriptor's buffer is shared by the actors reading it).
cat > "$dir/readers.st" <<'ST'
Object subclass: #Reader.
Reader >> count
  | n sum line |
  n := 0. sum := 0.
  [(line := Stdio stdin nextLine) notNil] whileTrue: [n := n + 1. sum := sum + line asNumber].
  ^ Array with: n with: sum
futures := (1 to: 4) collect: [:i | Reader new asActor count].
n := 0. sum := 0.
futures do: [:f | | r | r := f wait. n := n + (r at: 1). sum := sum + (r at: 2)].
Transcript showCr: n printString , ' ' , sum printString.
ST
out=$(seq 1 20000 | timeout 60 "$PROTOST" "$dir/readers.st" 2>&1) || true
[[ "$out" == "20000 200010000" ]] || { echo "FAIL concurrent readers: $out"; exit 1; }

# Actors blocked in network reads leave the collector's quorum: collections
# run meanwhile (small heap) and the program completes.
cat > "$dir/gc.st" <<'ST'
Import from: 'net'.
Object subclass: #Waiter.
Waiter >> waitOn: aSocket
  ^ aSocket receiveTimeout: 1500

sockets := (1 to: 4) collect: [:i | UDPSocket bindTo: 0].
futures := sockets collect: [:s | Waiter new asActor waitOn: s].
junk := nil.
1 to: 300000 do: [:i | junk := Array new: 8].
results := futures collect: [:f | f wait].
sockets do: [:s | s close].
Transcript showCr: (results select: [:r | r isNil]) size printString.
ST
out=$(PROTOCORE_HEAP_LIMIT_CELLS=300000 timeout 60 "$PROTOST" "$dir/gc.st")
[[ "$out" == "4" ]] || { echo "FAIL gc while blocked: $out"; exit 1; }

echo OK
