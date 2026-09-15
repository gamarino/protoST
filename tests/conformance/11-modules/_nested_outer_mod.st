"-- a module that imports _raising_mod, whose top level raises — used by"
"-- import-error-nested.st. The statement after the import must not run. --"
Import from: '_raising_mod'.
ImportProbe noteOuterAfter.
