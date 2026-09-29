"EXPECT: forwarded: size forwarded: frobnicate:with: cba"
"A class may override doesNotUnderstand: (proxies, forwarding)."
Object subclass: #Proxy instanceVariableNames: 'target'.
Proxy >> target: t
  target := t.
  ^ self.

Proxy >> doesNotUnderstand: aMessage
  aMessage selector = #reversed ifTrue: [^ target perform: aMessage selector withArguments: aMessage arguments].
  ^ 'forwarded: ' , aMessage selector.

p := Proxy new target: 'abc'.
p size , ' ' , (p frobnicate: 1 with: 2) , ' ' , p reversed.
