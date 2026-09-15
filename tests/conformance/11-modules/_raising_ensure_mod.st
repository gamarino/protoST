"-- a module whose top level raises an Error inside ensure: — used by"
"-- import-error-ensure-runs-once.st --"
ImportProbe noteRun.
[ Error signal: 'boom in module' ] ensure: [ ImportProbe noteCleanup ].
