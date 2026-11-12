"EXPECT-ERROR: NonRes: boom"
"§8.2 / §8.6: the default action of an unhandled exception whose class
 answers false to isResumable ends the activation, as for an Error; it does
 not resume with nil. Before the fix the override was ignored, the text was
 printed as a warning and the program continued."
Exception subclass: #NonRes.
NonRes >> isResumable
  ^ false.

NonRes signal: 'boom'.
'continued'
