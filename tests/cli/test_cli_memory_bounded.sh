#!/usr/bin/env bash
# With default settings the collector runs: a 3M-iteration allocating loop
# stays under 1 GB of resident memory (it reached 7-12 GB before 0.4.0).
set -u
PROTOST="$1"
# The mechanism under test: with a 10M-cell ceiling (640 MB of cells) the
# collector keeps an allocating loop under 1 GB; the test sets the ceiling
# itself so it does not depend on the default (configureHeap).
out_file="$(mktemp)"; trap 'rm -f "$out_file"' EXIT
program='s := 0. 1 to: 3000000 do: [:i | s := s + (Array new: 10) size]. s'
if command -v cygpath >/dev/null 2>&1 && [ ! -x /usr/bin/time ]; then
    # Git for Windows has no GNU time to measure the peak: there the loop is
    # only required to complete with the right result under the ceiling.
    PROTOCORE_HEAP_LIMIT_CELLS=10000000 "$PROTOST" -e "$program" >"$out_file"
    [ "$(cat "$out_file")" = "30000000" ] || { echo "FAIL: result [$(cat "$out_file")]"; exit 1; }
    echo "OK (result only: no /usr/bin/time to measure the peak)"; exit 0
fi
rss=$( { PROTOCORE_HEAP_LIMIT_CELLS=10000000 /usr/bin/time -f '%M' "$PROTOST" -e "$program" >"$out_file"; } 2>&1 | tail -1 )
# The loop must also have completed with the right result: a crash is not
# "bounded memory".
[ "$(cat "$out_file")" = "30000000" ] || { echo "FAIL: result [$(cat "$out_file")]"; exit 1; }
[[ "$rss" =~ ^[0-9]+$ ]] || { echo "FAIL: could not measure: $rss"; exit 1; }
(( rss < 1048576 )) || { echo "FAIL: maxrss ${rss} KB"; exit 1; }
echo "OK (maxrss ${rss} KB)"
