"EXPECT: #(1 16)"
"Spec §14.3 T2, compareAndSet: 16 actors race 16 different candidates
 through compareAndSet: #oid from: nil to: into one write-once variable,
 round after round on fresh holders. On every holder exactly one answers
 true, and every racer then reads the winner's candidate. Prints (true
 answers per holder, racers per holder that read the winner). Checked by
 count only."
Object subclass: #Holder instanceVariableNames: '' writeOnceNames: 'oid'.
Holder >> oid
  ^ oid

Object subclass: #Racer instanceVariableNames: ''.
Racer >> race: mine on: holders
  "Answer, per holder, {did my compareAndSet: answer true. what I read after}."
  ^ holders collect: [:h | | won |
      won := h compareAndSet: #oid from: nil to: mine.
      {won. h oid}]

| rounds holders values racers futures results wins readers |
rounds := 2000.
holders := (1 to: rounds) collect: [:i | Holder new].
values := (1 to: 16) collect: [:i | 'candidate-oid-' , i printString].
racers := (1 to: 16) collect: [:i | Racer new asActor].
futures := (1 to: 16) collect: [:i | (racers at: i) race: (values at: i) on: holders].
results := futures collect: [:f | f wait].
wins := 0. readers := 0.
1 to: rounds do: [:k | | winners |
  winners := (1 to: 16) select: [:i | (((results at: i) at: k) at: 1) == true].
  wins := wins + winners size.
  winners size = 1 ifTrue: [
    1 to: 16 do: [:i |
      ((((results at: i) at: k) at: 2) == (values at: winners first)
        and: [(holders at: k) oid == (values at: winners first)])
          ifTrue: [readers := readers + 1]]]].
(wins = rounds and: [readers = (16 * rounds)])
  ifTrue: [{wins // rounds. readers // rounds}]
  ifFalse: [{wins. readers} printString]
