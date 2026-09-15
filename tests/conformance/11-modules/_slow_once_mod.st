"-- a module whose top level counts its own runs and is slow to load, so that"
"-- concurrent importers overlap — used by import-concurrent-runs-once.st --"
LoadProbe bump.
Object sleep: 150.
Object subclass: #Once.
Once class >> answer
  ^ 7.
