"EXPECT: next"
"An unindented line after a method's indented body starts a new top-level
 statement, even without a blank line before it (it used to be swallowed
 into the method, silently)."
Object subclass: #A instanceVariableNames: 'log'.
A >> foo
  log := 1.
  #(1 2) do: [:e | log := log + e].
  ^ log
A new foo printNl.
'next' printNl.
