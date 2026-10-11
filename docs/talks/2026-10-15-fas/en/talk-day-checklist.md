# Checklist — talk day

## The night before

- [ ] Install one of two combinations on the talk laptop (both pass the
      four demos, verified on 11/10):
      - the one the performance report measured: protoCore 2.14.5 and
        protoST from `main` (`4876884`), local packages from
        `build_pkg_oct11`, not published;
      - the published release, the one the audience will install: protoCore
        2.6.2 and protoST 0.5.0. On Windows, inside WSL2 with Ubuntu 24.04:
        `sudo apt install ./protoCore-2.6.2-Linux.deb ./protost-0.5.0-Linux.deb`
        (`apt install ./…` also installs any missing dependency). The
        package requires `protocore (>= 2.6.1)`.
      The performance figures hold for both within the noise, except
      allocation-heavy actors on one worker, 4–6 % slower on `main` (the
      report runs them side by side).
- [ ] `protost --version` → `protoST 0.5.0`.
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
