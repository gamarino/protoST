# protoST at FAST, 15/10/2026 — English materials

English version of the materials for the protoST talk at the Fundación
Argentina de Smalltalk. The Spanish originals in the parent folder are the
reference; these files translate them without adding or removing content.

| File | Translated from | Contents |
|------|-----------------|----------|
| `script.md` | `../guion.md` | Talk script: 30-minute core and optional modules up to 60 minutes |
| `hard-questions.md` | `../preguntas-dificiles.md` | Honest answers to the hard questions likely to come up |
| `talk-day-checklist.md` | `../checklist-dia-de-la-charla.md` | Checklist for the night before, the hour before and during the talk |
| `deck.html` | the published slide deck (`../deck.html` is the Spanish one) | The 17 slides with their speaker notes (`<aside>`), as a single HTML file in the Slides format |
| `speaker-notes.md` | `../notas-del-orador.md` | The speaker notes of every slide, extracted from `deck.html` (the slide PDFs carry none) |

## Shared with the Spanish version

The demos and the recordings are not duplicated; both versions use the same
files:

- `../demos/` — `run_demo.sh`, `check_all.sh`, the four `.st` demos (demo 4 also has a Python `.feed`) and their
  expected outputs in `../demos/expected/`. The demo code, its comments and
  its output are already in English.
- `../recordings/` — terminal recordings of each demo, replayed with
  `scriptreplay` as the fallback if a live demo fails.

Paths in `script.md` and `talk-day-checklist.md` (for example
`cd docs/talks/2026-10-15-fas/demos` or `../recordings/`) refer to those
shared folders, exactly as in the Spanish version.

The figures come from three reports, named where each is used:
`benchmarks/reports/2026-10-12-adaptive-heap-default.md` (0.6.2 against
0.6.1: startup, actors, memory, demo 2),
`benchmarks/reports/2026-10-11-protocore-2.14.5.md` (the CPython
comparisons, measured on the 0.6.0 code) and
`benchmarks/reports/2026-10-11-actor-regression-bisection.md` (the noise
floor). They are copied as they appear there. Decimal commas in the Spanish
text are written as decimal points here (for example 4,45× becomes 4.45×); no
value was changed.
