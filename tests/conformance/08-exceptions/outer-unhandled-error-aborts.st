"EXPECT-ERROR: boom"
"§8.2 / §8.4: an Error is not resumable, so ex outer is ex pass (ANSI; Pharo
 sets no outer context for it). With no enclosing handler the Error's default
 action runs: the program's unhandled-error path, which ends the activation
 with the Error's text. The inner handler never continues."
[Error signal: 'boom']
  on: Error
  do: [:e | e outer. 'unreached'].
'unreached either'
