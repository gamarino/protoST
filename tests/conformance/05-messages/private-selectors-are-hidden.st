"EXPECT: #(false false nil #notUnderstood true '3')"
"A selector that begins with two underscores is private to the runtime (LANGUAGE.md 3.3): respondsTo:, canUnderstand: and lookupSelector: do not report it, and perform: does not send it. __st_send__, the entry another runtime sends through, is one (0.10.0)."
{ 3 respondsTo: '__st_send__' asSymbol.
  Object canUnderstand: '__st_send__' asSymbol.
  Object lookupSelector: '__st_send__' asSymbol.
  [ 3 perform: ('__st_send__' asSymbol) with: #printString ] on: MessageNotUnderstood do: [ :e | #notUnderstood ].
  3 respondsTo: #printString.
  3 perform: #printString }
