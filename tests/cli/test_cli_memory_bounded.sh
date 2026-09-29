#!/usr/bin/env bash
# With default settings the collector runs: a 3M-iteration allocating loop
# stays under 1 GB of resident memory (it reached 7-12 GB before 0.4.0).
set -u
PROTOST="$1"
# The mechanism under test: with a 10M-cell ceiling (640 MB of cells) the
# collector keeps an allocating loop under 1 GB. The default ceiling is larger
# (docs/STATUS.md, configureHeap), so the test sets its own.
rss=$( { PROTOCORE_HEAP_LIMIT_CELLS=10000000 /usr/bin/time -f '%M' "$PROTOST" -e 's := 0. 1 to: 3000000 do: [:i | s := s + (Array new: 10) size]. s' >/dev/null; } 2>&1 | tail -1 )
[[ "$rss" =~ ^[0-9]+$ ]] || { echo "FAIL: could not measure: $rss"; exit 1; }
(( rss < 1048576 )) || { echo "FAIL: maxrss ${rss} KB"; exit 1; }
echo "OK (maxrss ${rss} KB)"
