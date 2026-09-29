# Checklist — talk day

## The night before

- [ ] Install the 0.4.0 `.deb` on the talk laptop:
      `sudo dpkg -i protost-0.4.0-Linux.deb`. Install protoCore first:
      the package depends on `protocore (>= 2.5.0)` and `protocore (<< 3.0.0)`
      (`dpkg-deb -I protost-0.4.0-Linux.deb`); protoCore 2.5.0 is installed on
      the development machine.
- [ ] `protost --version` → `protoST 0.4.0`.
- [ ] `cd docs/talks/2026-10-15-fas/demos && RUNS=20 ./check_all.sh` → all
      three demos 20/20.
- [ ] Test the recordings:
      `scriptreplay --timing=../recordings/01-familiar-code.timing ../recordings/01-familiar-code.typescript`
      (and 02, 03).
- [ ] Export the deck PDF from the artifact and save it in this folder (it is
      not there yet); keep an offline copy of that PDF and of the repository.

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
