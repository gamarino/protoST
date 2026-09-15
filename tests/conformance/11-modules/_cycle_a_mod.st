"-- imports _cycle_b_mod, which imports this module back — used by the"
"-- import-cycle-*.st tests. The sleep lets two concurrent importers each"
"-- start one half of the cycle before either reaches the other half. --"
Object sleep: 100.
Import from: '_cycle_b_mod'.
Object subclass: #CycleA.
