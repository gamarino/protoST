# Checklist — talk day

## The night before

- [ ] Install the published release on the talk laptop, the one the
      audience will install: protoCore 2.14.5 and protoST 0.6.2 (the code
      the performance report measured plus the S24 fix and the adaptive heap controller as the default memory policy). On Windows, inside WSL2 with
      Ubuntu 24.04:
      ```
      wget https://github.com/numaes/protoCore/releases/download/v2.14.5/protoCore-2.14.5-Linux.deb
      wget https://github.com/gamarino/protoST/releases/download/v0.6.2/protost-0.6.2-Linux.deb
      sudo apt install ./protoCore-2.14.5-Linux.deb ./protost-0.6.2-Linux.deb
      ```
      (`apt install ./…` also installs any missing dependency, and upgrades
      a protoCore 2.6.2 left over from 0.5.0). protoST 0.6.2 requires
      protoCore 2.11.0 or later; 2.14.5 is the recommended release.
- [ ] `protost --version` → `protoST 0.6.2`;
      `ldd $(command -v protost) | grep protoCore` → `/usr/lib/x86_64-linux-gnu/libprotoCore.so.3`.
- [ ] `python3 --version` answers (demo 4 uses it).
- [ ] `cd docs/talks/2026-10-15-fas/demos && RUNS=20 ./check_all.sh` → all
      four demos 20/20.
- [ ] Test the recordings:
      `scriptreplay --timing=../recordings/01-familiar-code.timing ../recordings/01-familiar-code.typescript`
      (and 02, 03, 04).
- [ ] Keep an offline copy of the deck PDF (`slides/protoST-FAST-2026-en.pdf`,
      regenerated on 11/10 with the new figures) and of the repository.

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
