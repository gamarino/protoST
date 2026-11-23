"EXPECT: #(16 0)"
"Spec §14.3 T2, equal values: 16 actors race to store into one write-once
 variable, round after round on fresh holders. Each builds its own String of
 more than 6 bytes by concatenation; the 16 Strings are = and, checked
 pairwise, never ==. A write that finds the variable set is silent when its
 value is = to the installed one, so every racer answers the installed value
 itself and none signals. Prints (racers per holder whose answer is == the
 installed value, signals). Checked by count only."
Object subclass: #Holder instanceVariableNames: '' writeOnceNames: 'key'.
Holder >> setKey: x
  ^ key := x
Holder >> key
  ^ key

Object subclass: #Racer instanceVariableNames: ''.
Racer >> raceOn: holders
  "Answer {my string. the answer of each store, or #signalled}."
  | mine answers |
  mine := 'explicit-' , 'state-key'.
  answers := holders collect: [:h |
    [h setKey: mine] on: ModificationForbidden do: [:e | #signalled]].
  ^ {mine. answers}

| rounds holders racers futures results strings distinct identical signals |
rounds := 2000.
holders := (1 to: rounds) collect: [:i | Holder new].
racers := (1 to: 16) collect: [:i | Racer new asActor].
futures := racers collect: [:r | r raceOn: holders].
results := futures collect: [:f | f wait].
strings := results collect: [:r | r at: 1].
distinct := true.
1 to: 16 do: [:i | i + 1 to: 16 do: [:j |
  ((strings at: i) == (strings at: j)) ifTrue: [distinct := false].
  ((strings at: i) = (strings at: j)) ifFalse: [distinct := false]]].
identical := 0. signals := 0.
results do: [:r |
  (r at: 2) doWithIndex: [:answer :k |
    answer == #signalled
      ifTrue: [signals := signals + 1]
      ifFalse: [answer == (holders at: k) key ifTrue: [identical := identical + 1]]]].
(distinct and: [identical = (16 * rounds)])
  ifTrue: [{identical // rounds. signals}]
  ifFalse: [{distinct. identical. signals} printString]
