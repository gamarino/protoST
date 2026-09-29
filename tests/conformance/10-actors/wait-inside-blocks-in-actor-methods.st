"EXPECT: #(6 #(2 4 6) 10 2 'caught' 6)"
"A wait inside a block that a primitive evaluates (do:, collect:,
 inject:into:, ensure:, on:do:) in an actor method gives the same result as
 anywhere else: the loop sees every element, ensure: runs, handlers catch.
 (Such a wait blocks its worker, which runs other actors' messages meanwhile,
 instead of suspending through the primitive's native frames.)"
Object subclass: #Source.
Source >> read: n  ^ n.
Source >> fail  ^ 1 / 0.

Object subclass: #Reader instanceVariableNames: 'src'.
Reader >> initialize  src := Source new asActor.
Reader >> sumDo  | n | n := 0. #(1 2 3) do: [:i | n := n + (src read: i) wait]. ^ n.
Reader >> doubled  ^ #(1 2 3) collect: [:i | (src read: i * 2) wait].
Reader >> injected  ^ #(1 2 3 4) inject: 0 into: [:a :i | a + (src read: i) wait].
Reader >> ensured  | log | log := OrderedCollection new. [log add: (src read: 1) wait] ensure: [log add: #ensured]. ^ log size.
Reader >> handled  ^ [src fail wait] on: ZeroDivide do: [:e | 'caught'].
Reader >> loopVar  | b n | n := 0. b := [:i | n := n + (src read: i) wait]. 1 to: 3 do: b. ^ n.

| r |
r := Reader new asActor.
{ r sumDo wait. r doubled wait. r injected wait. r ensured wait. r handled wait. r loopVar wait }
