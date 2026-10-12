# Recorded fallbacks

Terminal recordings of each of the four demos, made with `script` (classic
timing files) on 2026-10-12 against protoST 0.6.3 (the source tree's Release
build of the release commit, GCC 15, protoCore 2.14.5), in the C locale so
the lines `script` adds are in English. A browser was open, so the timings
demo 2 shows are not idle figures (it printed a speedup of 4.5×). They are
not part of the `.deb`; they live in the repository. Replay one with:

    scriptreplay --timing=01-familiar-code.timing 01-familiar-code.typescript

Each ends with the `-- <demo>: verified` line printed by `run_demo.sh` after
checking the output against `../demos/expected/`.
