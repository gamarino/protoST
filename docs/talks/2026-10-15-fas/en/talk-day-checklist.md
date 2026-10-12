# Checklist — talk day

## The night before

- [ ] Install the published release on the talk laptop, the one the
      audience will install: protoCore 2.14.5 and protoST 0.6.3 (0.6.2, which
      made the adaptive heap controller the default memory policy, plus the
      correct release of transient pins in the dispatch loop, a clear error
      when the kernel is missing, and the demos inside the package). On
      Windows, inside WSL2 with Ubuntu 24.04:
      ```
      wget https://github.com/numaes/protoCore/releases/download/v2.14.5/protoCore-2.14.5-Linux.deb
      wget https://github.com/gamarino/protoST/releases/download/v0.6.3/protost-0.6.3-Linux.deb
      sudo apt install ./protoCore-2.14.5-Linux.deb ./protost-0.6.3-Linux.deb
      ```
      (`apt install ./…` also installs any missing dependency and upgrades a
      protoCore 2.6.2 left over from 0.5.0, or an earlier protoST). There are
      two floors: the protoST 0.6.3 `.deb` requires protoCore 2.14.5 or later
      (the package declares it); built from source, protoST 0.6.3 needs
      protoCore 2.11.0 or later.
- [ ] `protost --version` → `protoST 0.6.3`;
      `ldd $(command -v protost) | grep protoCore` →
      `libprotoCore.so.3 => /usr/bin/../lib/x86_64-linux-gnu/libprotoCore.so.3 (0x…)`:
      a path that ends in `lib/x86_64-linux-gnu/libprotoCore.so.3`, not
      `/usr/local` or a build directory.
- [ ] `python3 --version` answers (demo 4 uses it).
- [ ] The demos come with the package:
      `cd /usr/share/doc/protoST/talks/2026-10-15-fas/demos && RUNS=20 ./check_all.sh`
      → all four demos 20/20 (the same from the repository, in
      `docs/talks/2026-10-15-fas/demos`). `run_demo.sh` runs the `protost` on
      the `PATH`; to try another binary, `PROTOST=/path/to/protost`.
- [ ] Test the recordings (they are in the repository, not in the package):
      `scriptreplay --timing=../recordings/01-familiar-code.timing ../recordings/01-familiar-code.typescript`
      (and 02, 03, 04).
- [ ] Keep an offline copy of the deck PDF (`slides/protoST-FAST-2026-en.pdf`,
      regenerated on 12/10 with the 0.6.2 figures; also in
      `/usr/share/doc/protoST/talks/2026-10-15-fas/slides/`), of the speaker
      notes (`en/speaker-notes.md`) and of the repository.

## One hour before

- [ ] Close everything that uses CPU (browser with heavy tabs, builds,
      indexers). Demo 2 shows times measured on the spot.
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
