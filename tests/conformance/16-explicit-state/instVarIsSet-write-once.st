"EXPECT: #(false true true true)"
"Spec §6.8 and §6.3, conformance 101: on a write-once variable instVarIsSet:
 answers false before the first :=, true after it, and still true after an
 equal (not identical) second write, which loses silently."
Object subclass: #KeyNode instanceVariableNames: '' writeOnceNames: 'keySpec'.
KeyNode >> publish: aSpec
  keySpec := aSpec
KeyNode >> keySpec
  ^ keySpec

| n k1 k2 before afterFirst afterSecond |
k1 := 'spec-' , 'alpha'.
k2 := 'spec' , '-alpha'.
(k1 == k2) ifTrue: [Error signal: 'k1 and k2 are identical; the test would prove nothing'].
n := KeyNode basicNew.
before := n instVarIsSet: #keySpec.
n publish: k1.
afterFirst := n instVarIsSet: #keySpec.
n publish: k2.
afterSecond := n instVarIsSet: #keySpec.
{before. afterFirst. afterSecond. n keySpec == k1}
