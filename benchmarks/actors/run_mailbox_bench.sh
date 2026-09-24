#!/usr/bin/env bash
#
# run_mailbox_bench.sh — run the two mailbox send-cost benchmarks and print a
# markdown table.
#
# The runner, not the benchmark, is what makes a result trustworthy, so it:
#
#   * refuses to start while another proto* benchmark is running on the machine
#     (a benchmark measured against another benchmark is not a number);
#   * records the load average before and after, and says whether the run was
#     contended;
#   * checks every child's exit status, so a run killed by earlyoom can never
#     be counted as a pass;
#   * verifies the work each benchmark self-reports (VERIFIED == EXPECTED) and
#     aborts loudly on a mismatch, so a crash that exits 0 is never a win.
#
# Usage:
#   benchmarks/actors/run_mailbox_bench.sh [--runs N] [--label TEXT]
#
# Environment:
#   PROTOST_BIN     protost executable (default ./build_release/protost)
#   WORKER_SWEEP    worker counts for the contended benchmark
#                   (default "1 2 4 6")
#   WITH_HEADROOM   1 also runs mailbox_send_headroom.st (200 000 undrained
#                   messages). Opt-in because a mailbox representation that
#                   drains in O(n^2) is killed by the OOM reaper at that size,
#                   which is a recorded result, not an accident to trip over.
#   BENCH_TMPDIR    scratch directory (default <build_release>/bench-tmp);
#                   never /tmp, so a run leaves nothing outside the workspace.

set -u -o pipefail

runs=5
label=""
while [ $# -gt 0 ]; do
    case "$1" in
        --runs)  runs="$2"; shift 2 ;;
        --label) label="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
bin="${PROTOST_BIN:-$root/build_release/protost}"
worker_sweep="${WORKER_SWEEP:-1 2 4 6}"

if [ ! -x "$bin" ]; then
    echo "FATAL: protost executable not found or not executable: $bin" >&2
    exit 1
fi

# --- benchmark coordination -------------------------------------------------
# Wait until no sibling runtime's benchmark is on the machine. Nothing is
# killed or signalled: a competing run is somebody else's work.
#
# The matching is deliberately fussy, because a loose `pgrep -f` for these
# names is worse than no check at all. On this machine it also matches the
# earlyoom daemon (its --prefer list names protopy and protojs), this script
# itself, and any shell whose command line merely quotes those names -- for
# instance another agent's own wait-for-quiet script. Waiting for those never
# ends. So a candidate counts as a competitor only when the process really is
# one: its executable is a proto* runtime, or it is an interpreter running a
# known benchmark harness as its script argument.
runtime_comm='^(protoclj|protoclojure|protoscala|protoscalac|protopy|protopyc|protojs|protost)$'
harness_script='(run_benchmarks\.py|run_4way[^/]*|actor-bench[^/]*|.*_bench\.sh|bench\.sh|mailbox_send_[^/]*\.st)$'

# This script's own process ancestry (the shell, `timeout`, whatever wrapper
# launched it). Every one of them carries this script's path on its command
# line, so without this set the script waits for itself.
self_path="${BASH_SOURCE[0]}"
own_chain=" $$ "
_p="$$"
while [ -n "$_p" ] && [ "$_p" != "0" ] && [ "$_p" != "1" ]; do
    _p="$(awk '/^PPid:/ {print $2}' "/proc/$_p/status" 2>/dev/null)"
    [ -n "$_p" ] && own_chain="$own_chain$_p "
done
unset _p

# Every pid that is genuinely running a benchmark, this script's own process
# tree excluded.
competing_pids() {
    local pid comm argv
    for pid in $(ls -1 /proc 2>/dev/null | grep -E '^[0-9]+$'); do
        case "$own_chain" in *" $pid "*) continue ;; esac
        # Any process whose command line names THIS script is this script or a
        # wrapper around it. A second copy racing us is our own problem, not a
        # sibling's benchmark, so it is not something to wait for either.
        grep -qF "$self_path" "/proc/$pid/cmdline" 2>/dev/null && continue
        comm="$(cat "/proc/$pid/comm" 2>/dev/null)" || continue
        if printf '%s\n' "$comm" | grep -Eq "$runtime_comm"; then
            # One of our own children is not a competitor.
            [ "$(awk '/^PPid:/ {print $2}' "/proc/$pid/status" 2>/dev/null)" = "$$" ] && continue
            echo "$pid $comm"
            continue
        fi
        argv="$(tr '\0' '\n' < "/proc/$pid/cmdline" 2>/dev/null | sed -n '2,4p')"
        if [ -n "$argv" ] && printf '%s\n' "$argv" | grep -Eq "$harness_script"; then
            echo "$pid $comm"
        fi
    done
}

waited=0
while [ -n "$(competing_pids)" ]; do
    if [ "$waited" -eq 0 ]; then
        echo "Waiting for a competing benchmark to finish:" >&2
        competing_pids >&2
    fi
    sleep 10
    waited=$((waited + 10))
    if [ "$waited" -ge 1800 ]; then
        echo "FATAL: still contended after 30 min; refusing to measure." >&2
        exit 1
    fi
done
[ "$waited" -gt 0 ] && echo "Machine free after ${waited}s of waiting." >&2

load_before="$(uptime)"

# --- memory guard -----------------------------------------------------------
# earlyoom kills silently at 20% available. Refuse to start below 25%.
avail_pct() {
    awk '/^MemAvailable:/ {a=$2} /^MemTotal:/ {t=$2} END {printf "%d", (a*100)/t}' /proc/meminfo
}
if [ "$(avail_pct)" -lt 25 ]; then
    echo "FATAL: only $(avail_pct)% of memory available; refusing to measure." >&2
    exit 1
fi

median() {
    sort -n | awk '{v[NR]=$1} END {
        if (NR == 0) { print "NA"; exit }
        if (NR % 2) print v[(NR+1)/2]; else printf "%d", (v[NR/2] + v[NR/2+1]) / 2
    }'
}

field() { sed -n "s/^$2 \\([0-9-]*\\)\$/\\1/p" "$1" | tail -1; }

tmp="${BENCH_TMPDIR:-$(dirname "$bin")/bench-tmp}"
rm -rf "$tmp"
mkdir -p "$tmp" || { echo "FATAL: cannot create $tmp" >&2; exit 1; }

# run_case <name> <script> <worker-env-or-empty>
# Prints "<median-elapsed-ms> <median-total-ms> <sends>" on success.
run_case() {
    local name="$1" script="$2" workers="$3"
    local i out status elapsed total sends verified expected
    : > "$tmp/elapsed"
    : > "$tmp/total"
    sends=""
    for i in $(seq 1 "$runs"); do
        out="$tmp/$name.$i.out"
        if [ -n "$workers" ]; then
            PROTOST_WORKERS="$workers" "$bin" "$script" > "$out" 2>&1
        else
            "$bin" "$script" > "$out" 2>&1
        fi
        status=$?
        if [ "$status" -ne 0 ]; then
            echo "FATAL: $name run $i exited $status (killed? see $out)" >&2
            sed -n '1,20p' "$out" >&2
            return 1
        fi
        verified="$(field "$out" VERIFIED)"
        expected="$(field "$out" EXPECTED)"
        if [ -z "$verified" ] || [ -z "$expected" ] || [ "$verified" != "$expected" ]; then
            echo "FATAL: $name run $i did not do its work: VERIFIED='$verified' EXPECTED='$expected'" >&2
            sed -n '1,20p' "$out" >&2
            return 1
        fi
        elapsed="$(field "$out" ELAPSED_US)"
        total="$(field "$out" TOTAL_US)"
        [ -z "$total" ] && total="$(field "$out" DRAIN_US)"
        sends="$expected"
        echo "$elapsed" >> "$tmp/elapsed"
        echo "$total"   >> "$tmp/total"
    done
    echo "$(median < "$tmp/elapsed") $(median < "$tmp/total") $sends"
}

echo "# protoST mailbox send-cost benchmark"
echo
[ -n "$label" ] && { echo "**$label**"; echo; }
echo "- binary: \`$bin\`"
echo "- runs per case: $runs (median reported)"
echo "- load before: \`$load_before\`"
echo

echo "## Uncontended enqueue (worker pool stopped, one producer)"
echo
echo "| Case | Sends | Enqueue median (us) | ns / send | Drain median (us) |"
echo "|---|---|---|---|---|"
if r="$(run_case uncontended "$here/mailbox_send_cost.st" "")"; then
    set -- $r
    printf '| single producer | %s | %s | %.0f | %s |\n' "$3" "$1" \
        "$(awk -v us="$1" -v n="$3" 'BEGIN {print (us * 1000.0) / n}')" "$2"
else
    echo "| single producer | FAILED | - | - | - |"
    failed=1
fi
if [ "${WITH_HEADROOM:-0}" = "1" ]; then
    if r="$(run_case headroom "$here/mailbox_send_headroom.st" "")"; then
        set -- $r
        printf '| single producer, 200k backlog | %s | %s | %.0f | %s |\n' "$3" "$1" \
            "$(awk -v us="$1" -v n="$3" 'BEGIN {print (us * 1000.0) / n}')" "$2"
    else
        echo "| single producer, 200k backlog | **did not complete** | - | - | - |"
        headroom_failed=1
    fi
fi
echo

echo "## Contended enqueue (8 producer actors into one sink)"
echo
echo "| PROTOST_WORKERS | Sends | Enqueue median (us) | ns / send | Full drain median (us) |"
echo "|---|---|---|---|---|"
for w in $worker_sweep; do
    if r="$(run_case "contended-w$w" "$here/mailbox_send_contended.st" "$w")"; then
        set -- $r
        printf '| %s | %s | %s | %.0f | %s |\n' "$w" "$3" "$1" \
            "$(awk -v us="$1" -v n="$3" 'BEGIN {print (us * 1000.0) / n}')" "$2"
    else
        echo "| $w | FAILED | - | - | - |"
        failed=1
    fi
done
echo

load_after="$(uptime)"
echo "- load after: \`$load_after\`"
if [ -n "$(competing_pids)" ]; then
    echo "- **contended: a competing benchmark appeared during the run; these numbers are not trustworthy.**"
else
    echo "- contended: no competing proto* benchmark was seen before, during or after the run."
fi

if [ "${headroom_failed:-0}" -ne 0 ]; then
    echo
    echo "The 200k-backlog case did not complete on this build. With the"
    echo "ProtoList mailbox that is the expected outcome (quadratic drain,"
    echo "killed by the OOM reaper); with ProtoMPSCQueue it is a regression."
fi

if [ "${failed:-0}" -ne 0 ]; then
    echo
    echo "**One or more cases FAILED — do not read this table as a result.**"
    exit 1
fi
