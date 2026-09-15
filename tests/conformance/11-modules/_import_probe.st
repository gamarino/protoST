"-- ImportProbe: counters shared by the import-error-*.st tests and the"
"-- modules they import. Importing this module declares the global class. --"
Object subclass: #ImportProbe
  instanceVariableNames: ''
  classVariableNames: 'runs handled wrong cleanups passes outerAfter resumed failing expected'.

ImportProbe class >> reset
  runs := 0.
  handled := 0.
  wrong := 0.
  cleanups := 0.
  passes := 0.
  outerAfter := 0.
  resumed := nil.
  failing := false.
  expected := 'boom in module'.

"-- a module's top level ran --"
ImportProbe class >> noteRun
  runs := runs + 1.

"-- a handler ran; count it, and count it as wrong if the text is unexpected --"
ImportProbe class >> noteHandled: aText
  handled := handled + 1.
  (aText = expected) ifFalse: [ wrong := wrong + 1 ].

ImportProbe class >> noteCleanup
  cleanups := cleanups + 1.

ImportProbe class >> notePass
  passes := passes + 1.

ImportProbe class >> noteOuterAfter
  outerAfter := outerAfter + 1.

ImportProbe class >> resumedWith: aValue
  resumed := aValue.

ImportProbe class >> failing: aBoolean
  failing := aBoolean.

ImportProbe class >> expect: aText
  expected := aText.

ImportProbe class >> runs  ^ runs.
ImportProbe class >> handled  ^ handled.
ImportProbe class >> wrong  ^ wrong.
ImportProbe class >> cleanups  ^ cleanups.
ImportProbe class >> passes  ^ passes.
ImportProbe class >> outerAfter  ^ outerAfter.
ImportProbe class >> resumed  ^ resumed.
ImportProbe class >> failing  ^ failing.

ImportProbe reset.
