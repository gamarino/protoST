"-- a module whose top level raises an Error — used by the import-error-*.st"
"-- tests. The importer must have imported _import_probe first. --"
ImportProbe noteRun.
Error signal: 'boom in module'.
ImportProbe noteOuterAfter.
