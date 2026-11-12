"S40: many client actors each wait, inside a block, for a reply from one
 server actor; the program waits for every client. Each blocked client buries
 the next one it helps on its worker, so the workers reach the helping cap
 while the server's messages are still queued. Every reply must arrive. The
 number of clients comes from the command line (default 200); the answer is
 that number."
Object subclass: #Server.
Server >> ping
  ^ 1.
Object subclass: #Client instanceVariableNames: 'srv'.
Client >> srv: s
  srv := s.
Client >> run
  #(1) do: [:i | ^ srv ping wait].
  ^ 0.

| n srv cs fs |
n := Smalltalk arguments isEmpty ifTrue: [200] ifFalse: [Smalltalk arguments first asNumber].
srv := Server new asActor.
cs := (1 to: n) collect: [:i | (Client new srv: srv; yourself) asActor].
fs := cs collect: [:c | c run].
(fs inject: 0 into: [:a :f | a + f wait]) printString
