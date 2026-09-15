"-- imports _cycle_a_mod, which imports this module back — used by the"
"-- import-cycle-*.st tests. --"
Object sleep: 100.
Import from: '_cycle_a_mod'.
Object subclass: #CycleB.
