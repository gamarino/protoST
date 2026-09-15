"-- a module whose top level signals a resumable Warning and records the"
"-- value signal returns — used by import-warning-resume.st --"
ImportProbe noteRun.
ImportProbe resumedWith: (Warning signal: 'careful').
Object subclass: #AfterWarning.
AfterWarning class >> answer
  ^ 7.
