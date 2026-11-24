"EXPECT: #('instVarIsSet: an Account has no instance variable ''ownr''' 'instVarIsSet: a SmallInteger has no instance variable ''x''' 'instVarIsSet: expects a String or Symbol, not 3' 'instVarIsSet: expects a String or Symbol, not nil' 'instVarIsSet: nil has no instance variable ''x''' 'instVarIsSet: Account has no instance variable ''owner''' 'Error' 'Error' nil)"
"Spec §6.8, M15, conformance 104: a name the receiver does not declare is
 never answered false (a misspelt name would read as not set yet); it
 signals Error with M15's text, and so does any name on an object without
 instance variables such as 3. A non-name argument gives M15's other text.
 On a class object the declared names are its class-instance variables
 (pre-flight ruling C10), so an instance-side name is unknown there, and
 the class is named by its bare name (C7). Nothing is created by a refusal."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.

| a text |
a := Account basicNew.
text := [:block | block on: Error do: [:e | e messageText]].
{text value: [a instVarIsSet: #ownr].
 text value: [3 instVarIsSet: #x].
 text value: [a instVarIsSet: 3].
 text value: [a instVarIsSet: nil].
 text value: [nil instVarIsSet: #x].
 text value: [Account instVarIsSet: #owner].
 [a instVarIsSet: #ownr] on: Error do: [:e | e class name].
 [3 instVarIsSet: #x] on: Error do: [:e | e class name].
 a instVarNamed: #ownr}
