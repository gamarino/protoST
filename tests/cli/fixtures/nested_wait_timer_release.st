"S40 (review I-1; from the reviewer's probe): with one configured worker,
 worker 0 holds 17 actors whose top one waits (nested) on a timer; chain Z
 fills the other 255 workers and needs one more turn, which worker 0 runs as
 soon as the timer fires. The program can finish, so no deadlock may be
 signalled, however early or late the timer fires. Arguments: the length of
 Z and the timer's delay in ms; the answer is 17 + the length of Z."
Object subclass: #Link instanceVariableNames: 'next zhead box'.
Link >> next: n
  next := n.
Link >> zhead: z box: b
  zhead := z. box := b.
Link >> ping
  zhead notNil ifTrue: [ ^ self kick ].
  next isNil ifTrue: [^ 1].
  #(1) do: [:i | ^ 1 + (next ping wait)].
  ^ 0.
Link >> kick
  | zf |
  zf := zhead ping.
  box value: zf ifCurrent: nil.
  #(1) do: [:i | (Future after: (Smalltalk arguments at: 2) asNumber) wait ].
  ^ 1.

| n m links zs box r |
n := 17.
m := (Smalltalk arguments first) asNumber.
box := Atom on: nil.
links := (1 to: n) collect: [:i | Link new asActor].
zs := (1 to: m) collect: [:i | Link new asActor].
1 to: n - 1 do: [:i | ((links at: i) next: (links at: i + 1)) wait].
1 to: m - 1 do: [:i | ((zs at: i) next: (zs at: i + 1)) wait].
((links at: n) zhead: zs first box: box) wait.
r := links first ping wait.
(r + box value wait) printString
