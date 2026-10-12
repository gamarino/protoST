# The 4-6 % actor "regression" of 2026-10-11 — bisection

**Synthetic benchmarks, one notebook-class machine** (DEV12: AMD Ryzen 5
5500U, 6 cores / 12 threads). Raw data and scripts:
[`data/2026-10-11/bisection/`](data/2026-10-11/bisection/).

## Question

The [re-measurement on protoCore 2.14.5](2026-10-11-protocore-2.14.5.md)
found allocation-heavy actor workloads with one worker 4-6 % slower on
`main / 2.14.5` than on the 2026-10-03 build (`5badd33` on protoCore 2.10.2):
`parallel_speedup` with one worker (+5-6 %), `cooperative_yield` with two
(+4 %), `saturation_big` with one (+5 %). Turning off pacing, the multi-cursor
sweep or instance-variable write groups did not recover it. Where does it
come from?

## Answer

**On a quiet machine, with every compared build made by the same compiler,
there is no wall-time regression larger than the measurement's own noise
floor.** Identical binaries run as A/A pairs differ by 1-4 % in this session,
the same size as the effect being chased; nothing below is a finding unless
it is larger than its A/A pair, and the only differences that are:

- **protoCore 2.12.0 and later spend more CPU than 2.10.2 / 2.11.0 on
  `saturation_big`, not more wall time.** Same protoST binary, protoCore
  swapped with `LD_LIBRARY_PATH` (checked with `ldd`): the collector runs one
  more cycle (5 instead of 4, `PROTOCORE_HEAP_TRACE=1`: pacing starts one
  early; with `PROTOCORE_GC_PACING=0` it is 4 again), each sweeping the full
  10 M-cell heap; voluntary context switches go from about 250 to about 340;
  CPU time over all threads is +4 % (quiet session) while wall time is -4 %.
  On an idle machine that extra collector work runs on an idle core. On a
  machine where every core is busy -- as in the original sessions, at load
  2.9-6.7 with other agents building -- it competes with the mutator. That
  is a plausible explanation of the original 4-6 %, consistent with every
  run here; it is **not proven**, because the original sessions cannot be
  replayed (the machine has since been upgraded, see Method).
- **`parallel_speedup` with one worker: `main` is about 2-3 % slower than the
  2026-10-03 protoST on the same protoCore**, in three quiet sessions, against
  A/A spreads of 0.7-1.8 %. Instance-variable write groups are not the
  cause: this workload assigns no instance variable, and turning groups off
  recovered it in one session (g15, +1.5 % against +2.7 / +4.7 %) and not in
  the next (g15sat: +3.3 % against +2.4 / +3.0 %). The one source change
  between the two binaries (`207bdeb`) adds a few fields to every frame and
  code to the dispatch loop; without hardware counters this session cannot
  say whether the 2-3 % is that extra work or code layout, and at this size
  it is not worth a change.

**No fix.** Nothing found is both real and fixable without giving up what
the change bought: pacing's earlier cycle trades CPU on another core for
shorter mutator waits, which was protoCore's decision in 2.12.0 (and
re-examined in its A2 adjustment, 2026-10-04).

## Method

- **The machine changed during the work.** It was restarted and upgraded from
  Ubuntu 24.04 (GCC 13.3) to 26.04 (GCC 15.2). After the upgrade
  `perf_event_paranoid` is 4 and there is no password-less `sudo`, so
  **hardware counters (instructions, cycles, cache misses) were not
  available**; the maintainer can restore them with
  `sudo sysctl kernel.perf_event_paranoid=1`. The figures are wall time,
  CPU time over all threads (`ru_utime`, `ru_stime` from `wait4`), context
  switches, minor faults and peak RSS, medians of 7-9 interleaved runs
  (`perfbench.py`: every run verified, warm-up discarded, order rotated each
  round, load and competing processes recorded per run).
- **Same compiler for every build compared.** Two sets: (1) the binaries of
  the original report, all GCC 13.3 (the installed 2026-10-03 `protost`,
  `build_bench` at `4876884`, protoCore packages 2.10.2, 2.11.0, 2.14.1-2.14.5
  built before the upgrade; 2.12.0, 2.13.0 and 2.14.0 rebuilt with
  `g++-13`, 13.4); (2) everything rebuilt from `git archive` with GCC 15.2:
  protoCore 2.10.2, 2.11.0, 2.12.0, 2.13.0, 2.14.0, 2.14.1, 2.14.2, 2.14.5 and
  protoST `5badd33`, `207bdeb`, `4876884` (`build_cores.sh`, `build_st.sh`).
  `207bdeb` and `4876884` produce byte-identical `protost` binaries (only
  CMake text changed between them), which makes them a free A/A pair.
- **Quiet.** The harness waits before each run for the one-minute load to
  drop below 3-4 and for no compiler, test runner or other protoCore runtime
  to be running; the tables mark how many runs it had to start anyway. The
  quiet sessions below had none.
- Before the restart one session had hardware counters (GCC 13 protoST
  binary of 2026-10-03, GCC 15 protoCore builds, busy machine): on
  `saturation_big` with one worker, instructions over all threads were
  26.32 G on 2.10.2 and 2.11.0 and 27.33-27.71 G from 2.12.0 on (+4 to +5 %),
  `parallel_speedup` 5.73-5.75 G on every version (flat). That session's raw
  files were in `/tmp` and were lost with the restart; the numbers are
  quoted from its console output.

## Results

### Axis (a): protoCore versions, same protoST binary

GCC 13.3 binary of 2026-10-03 (`5badd33`), `parallel_speedup` with one
worker, quiet (0 of 63 runs started under load or with a competitor):

| protoCore | 2.10.2 | 2.11.0 | 2.12.0 | 2.13.0 | 2.14.0 | 2.14.1 | 2.14.2 | 2.14.5 | 2.14.5 (A/A) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| wall ms | 914 | 920 | 929 | 928 | 906 | 930 | 902 | 902 | 905 |
| vs 2.10.2 | | +0.7 % | +1.6 % | +1.6 % | -0.9 % | +1.7 % | -1.3 % | -1.3 % | -1.0 % |

`saturation_big` with one worker in the same session ran under load (every
run had a competitor): wall 3,487-3,965 ms, no order by version; the two
copies of 2.14.5 differ by 2.5 %. A second session over 2.14.2-2.14.5 (GCC
13.3, `axisA2`) put 2.14.3 -- which has no library code change against
2.14.2 -- 8 % faster than 2.14.2: that is the noise of a loaded machine on
this workload.

### Axis (b): protoST commits, same protoCore (2.14.5)

GCC 13.3, the original report's own binaries, `parallel_speedup` quiet:

| | 2026-10-03 | `main` | `main`, groups off | 2026-10-03 (A/A) | `main` (A/A) |
|---|---:|---:|---:|---:|---:|
| wall ms | 891 | 906 | 901 | 885 | 922 |

### Both axes, GCC 15.2, quiet (`g15sat`, 0 of 126 runs under load)

| Configuration | `saturation_big` w=1: wall / CPU ms | vcsw | `parallel_speedup` w=1: wall / CPU ms |
|---|---:|---:|---:|
| 2026-10-03 on 2.10.2 | 3,150 / 2,901 | 256 | 848 / 504 |
| 2026-10-03 on 2.10.2 (A/A) | 3,113 / 2,860 | 238 | 863 / 506 |
| 2026-10-03 on 2.14.5 | 3,018 / 3,018 | 343 | 859 / 505 |
| same, no sweep helpers | 2,982 / 3,011 | 345 | 867 / 510 |
| same, no pacing | 3,223 / 3,324 | 277 | 875 / 514 |
| `main` on 2.14.5 | 3,128 / 3,164 | 356 | 868 / 516 |
| `main` on 2.14.5 (A/A) | 3,035 / 3,072 | 402 | 873 / 522 |
| `main`, groups off | 3,058 / 3,100 | 396 | 876 / 523 |
| `main`, no sweep helpers | 3,085 / 3,083 | 339 | 865 / 512 |

Cycles per run of `saturation_big` with one worker (`PROTOCORE_HEAP_TRACE=1`):
4 on 2.10.2 and 2.11.0 (pacing on or off), 5 on 2.12.0 and 2.14.5, 4 on both
with `PROTOCORE_GC_PACING=0`. Each cycle walks the full 10 M-cell heap
(`heap=10000000`) and lasts 67-123 ms on the collector thread; in every
cycle the mutator still waits once for headroom (`waits=1`).

## Found on the way

The bisection's fib runs led to a separate, real defect: a program that only
recurses reached no garbage-collection safepoint, so its garbage was never
reclaimed (fib 29 ran out of memory under the default limit). Fixed in
0.6.1; see [`2026-10-11-fib-under-adaptive-controller.md`](2026-10-11-fib-under-adaptive-controller.md).
