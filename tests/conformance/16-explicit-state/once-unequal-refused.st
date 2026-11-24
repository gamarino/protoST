"EXPECT: #('the write-once variable ''key'' of a Holder already holds a different value; nothing was written' 'alpha-key')"
"Spec §6.3, M12, conformance 62: a write-once write whose value is not = to
 the installed one signals ModificationForbidden in place (pre-flight ruling
 C1), and the variable keeps the installed value (ruling C13: one line)."
Object subclass: #Holder instanceVariableNames: '' writeOnceNames: 'key'.
Holder >> setKey: x
  ^ key := x
Holder >> key
  ^ key

| o refused |
o := Holder new.
o setKey: 'alpha-key'.
refused := [o setKey: 'beta-key'] on: ModificationForbidden do: [:e | e messageText].
{ refused. o key }
