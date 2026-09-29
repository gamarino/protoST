# Recorded fallbacks

Terminal recordings of each of the four demos, made with `script` on
2026-09-29 against protoST 0.5.0, in the C locale so the lines `script` adds
are in English (the machine had other applications running, so the timings
shown by demo 2 are not the idle figures). Replay one with:

    scriptreplay --timing=01-familiar-code.timing 01-familiar-code.typescript

Each ends with the `-- <demo>: verified` line printed by `run_demo.sh` after
checking the output against `../demos/expected/`.
