#!/usr/bin/env bash
# With default settings the collector runs: a 3M-iteration allocating loop
# stays under 1 GB of resident memory (it reached 7-12 GB before 0.4.0).
set -u
PROTOST="$1"
# The mechanism under test: with a 10M-cell ceiling (640 MB of cells) the
# collector keeps an allocating loop under 1 GB; the test sets the ceiling
# itself so it does not depend on the default (configureHeap).
#
# The peak is the one protost reports itself when PROTOST_REPORT_PEAK_RSS=1
# (src/main.cpp): getrusage's ru_maxrss on Linux and macOS -- the figure GNU
# and BSD time print -- and the peak working set on Windows, where no time(1)
# exists. One measurement, the same bound, on every platform.
out_file="$(mktemp)"; err_file="$(mktemp)"; trap 'rm -f "$out_file" "$err_file"' EXIT
program='s := 0. 1 to: 3000000 do: [:i | s := s + (Array new: 10) size]. s'
PROTOCORE_HEAP_LIMIT_CELLS=10000000 PROTOST_REPORT_PEAK_RSS=1 "$PROTOST" -e "$program" >"$out_file" 2>"$err_file"
rc=$?
# The loop must also have completed with the right result: a crash is not
# "bounded memory".
[[ $rc -eq 0 && "$(cat "$out_file")" == "30000000" ]] || { echo "FAIL: rc=$rc result [$(cat "$out_file")] $(cat "$err_file")"; exit 1; }
rss=$(sed -n 's/^protost: peak resident set size \([0-9][0-9]*\) KB$/\1/p' "$err_file" | tr -d '\r')
[[ "$rss" =~ ^[0-9]+$ ]] || { echo "FAIL: no peak reported: $(cat "$err_file")"; exit 1; }
# A plausibility floor: the loop fills a large part of its 640 MB ceiling
# before the collector runs, so a report of a few megabytes would mean the
# measurement, not the collector, is wrong.
(( rss > 65536 )) || { echo "FAIL: implausible peak ${rss} KB"; exit 1; }
(( rss < 1048576 )) || { echo "FAIL: maxrss ${rss} KB"; exit 1; }
echo "OK (maxrss ${rss} KB)"
