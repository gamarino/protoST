# Checklist — talk day

## The night before

- [ ] Install protoCore 2.6.1 and protoST 0.5.0 on the talk laptop. On
      Windows, inside WSL2 with Ubuntu 24.04:
      `sudo apt install ./protoCore-2.6.1-Linux.deb ./protost-0.5.0-Linux.deb`
      (`apt install ./…` also installs any missing dependency). The package
      requires `protocore (>= 2.6.1)`.
- [ ] `protost --version` → `protoST 0.5.0`.
- [ ] `python3 --version` answers (demo 4 uses it).
- [ ] `cd docs/talks/2026-10-15-fas/demos && RUNS=20 ./check_all.sh` → all
      four demos 20/20.
- [ ] Test the recordings:
      `scriptreplay --timing=../recordings/01-familiar-code.timing ../recordings/01-familiar-code.typescript`
      (and 02, 03, 04).
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
