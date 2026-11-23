"EXPECT: #(1 15 true)"
"Spec §14.3 T2, unequal values: 16 actors race to store 16 different values
 through := into one write-once variable, round after round on fresh holders.
 On every holder exactly one write installs, the 15 others signal M12
 (ModificationForbidden), and the variable holds the value of the one writer
 that saw no signal. Prints (installs per holder, signals per holder, every
 holder holds its installer's value). Checked by count only."
Object subclass: #Holder instanceVariableNames: '' writeOnceNames: 'key'.
Holder >> setKey: x
  ^ key := x
Holder >> key
  ^ key

Object subclass: #Racer instanceVariableNames: ''.
Racer >> race: mine on: holders
  "Answer, per holder, #installed when the store answered my own value
   without a signal, #signalled on M12, and the answer itself otherwise."
  ^ holders collect: [:h |
      [ | answer | answer := h setKey: mine. answer == mine ifTrue: [#installed] ifFalse: [answer]]
        on: ModificationForbidden do: [:e | #signalled]]

| rounds holders values racers futures results installs signals holdsWinner other |
rounds := 2000.
holders := (1 to: rounds) collect: [:i | Holder new].
values := (1 to: 16) collect: [:i | 'racer-value-' , i printString].
racers := (1 to: 16) collect: [:i | Racer new asActor].
futures := (1 to: 16) collect: [:i | (racers at: i) race: (values at: i) on: holders].
results := futures collect: [:f | f wait].
installs := 0. signals := 0. holdsWinner := true. other := 0.
1 to: rounds do: [:k | | winners |
  winners := OrderedCollection new.
  1 to: 16 do: [:i | | outcome |
    outcome := (results at: i) at: k.
    outcome == #installed
      ifTrue: [winners add: i. installs := installs + 1]
      ifFalse: [outcome == #signalled
        ifTrue: [signals := signals + 1]
        ifFalse: [other := other + 1]]].
  (winners size = 1 and: [(holders at: k) key == (values at: winners first)])
    ifFalse: [holdsWinner := false]].
(installs = rounds and: [signals = (15 * rounds) and: [other = 0]])
  ifTrue: [{installs // rounds. signals // rounds. holdsWinner}]
  ifFalse: [{installs. signals. other. holdsWinner} printString]
