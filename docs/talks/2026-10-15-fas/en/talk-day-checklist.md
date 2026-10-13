# Checklist — talk day

## The night before

- [ ] Install the published release on the talk laptop, the one the
      audience will install: protoCore 2.14.6 and protoST 0.6.5 (0.6.2, which
      made the adaptive heap controller the default memory policy, plus
      0.6.3's correct release of transient pins in the dispatch loop and
      clear error when the kernel is missing, the demos inside the package,
      in `/usr/share/protoST/talks/`, and, in 0.6.5, protoCore 2.14.6, whose
      integer arithmetic creates no temporaries). On
      Windows, inside WSL2 with Ubuntu 24.04:
      ```
      wget https://github.com/numaes/protoCore/releases/download/v2.14.6/protoCore-2.14.6-Linux.deb
      wget https://github.com/gamarino/protoST/releases/download/v0.6.5/protost-0.6.5-Linux.deb
      sudo apt install ./protoCore-2.14.6-Linux.deb ./protost-0.6.5-Linux.deb
      ```
      (`apt install ./…` also installs any missing dependency and upgrades an
      earlier protoCore, such as 0.5.0's 2.6.2, or an earlier protoST). There are two floors: the protoST 0.6.5 `.deb`
      requires protoCore 2.14.6 or later (the package declares it); built
      from source, protoST 0.6.5 needs protoCore 2.11.0 or later.
- [ ] `protost --version` → `protoST 0.6.5`;
      `ldd $(command -v protost) | grep protoCore` →
      `libprotoCore.so.3 => /usr/bin/../lib/x86_64-linux-gnu/libprotoCore.so.3 (0x…)`:
      a path that ends in `lib/x86_64-linux-gnu/libprotoCore.so.3`, not
      `/usr/local` or a build directory.
- [ ] `python3 --version` answers (demo 4 uses it).
- [ ] The demos come with the package:
      `cd /usr/share/protoST/talks/2026-10-15-fas/demos && RUNS=20 ./check_all.sh`
      → all four demos 20/20 (the same from the repository, in
      `docs/talks/2026-10-15-fas/demos`). `run_demo.sh` runs the `protost` on
      the `PATH`; to try another binary, `PROTOST=/path/to/protost`.
- [ ] Test the recordings (they are in the repository, not in the package):
      `scriptreplay --timing=../recordings/01-familiar-code.timing ../recordings/01-familiar-code.typescript`
      (and 02, 03, 04).
- [ ] Keep an offline copy of the deck PDF (`slides/protoST-FAST-2026-en.pdf`,
      regenerated on 12/10 with the protoCore 2.14.6 figures; also in
      `/usr/share/protoST/talks/2026-10-15-fas/slides/`), of the speaker
      notes (`en/speaker-notes.md`) and of the repository.

## One hour before

- [ ] Close everything that uses CPU (browser with heavy tabs, builds,
      indexers). Demo 2 shows times measured on the spot: with the machine
      busy the speedup falls (1.6–3.2× measured on 12/10 with an editor's
      indexer using the CPU, instead of about 3.8×).
- [ ] Laptop plugged in and the power profile set to performance
      (`powerprofilesctl set performance`). On battery with the balanced
      profile the 12-actor half is power-limited: on 12/10 on DEV12, with the
      machine idle, demo 2 showed 1.9–3.7× (median ≈2.4×) instead of about
      3.8×. Check with `cat /sys/class/power_supply/*/online` (1 = plugged in).
- [ ] Terminal: large font (≥ 20 pt), light or dark background depending on
      the projector, full-screen window.
- [ ] `cd` to the demos folder; have `less 01-familiar-code.st` ready to show
      the code before running it.
- [ ] Turn off notifications.

## During

- [ ] If a demo fails or is slow: do not debug live. Say so and switch to the
      recording (`scriptreplay`).
- [ ] Quote the figures that appear on screen or those in the report, never
      from memory.
