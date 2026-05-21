"EXPECT: 50"
"-- 02_subclass_imported.st ------------------------------------------------"
"-- An imported module class is an ordinary class object: it can be         --"
"-- subclassed in the importing program, and `super` in an override reuses  --"
"-- the module's implementation. `Square` subclasses the imported           --"
"-- `Rectangle`, fixes both sides to one length, and reuses `area`.         --"
"-- Expected output: 50  (a 5x5 square plus a 5x5 from the doubled call)    --"

geometry := Import from: '_geometry'.

"Subclass an imported class — the receiver is an expression, so the
 message form of `subclass:` applies."
geometry Rectangle subclass: #Square instanceVariableNames: ''.

Square >> side: s
  "Reuse the imported Rectangle's setter via the inherited method."
  self setWidth: s height: s.
  ^ self.

Square >> doubleArea
  "super area reaches the imported Rectangle>>area."
  ^ (super area) * 2.

sq := Square new.
sq side: 5.
sq doubleArea.
