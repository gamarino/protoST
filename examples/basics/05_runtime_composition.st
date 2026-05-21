"EXPECT: HELLO and goodbye"
"-- 05_runtime_composition.st ----------------------------------------------"
"-- On-the-fly behaviour composition with `addBehavior:`. A behaviour is    --"
"-- just a class carrying methods; `addBehavior:` adds it as a parent of an --"
"-- existing class at runtime, with no recompilation.                       --"
"--                                                                        --"
"-- Note (STATUS.md D21): `addBehavior:` affects FUTURE instances — so the  --"
"-- Greeter instance is created AFTER both behaviours are composed in.      --"
"-- Expected output: HELLO and goodbye                                      --"

Object subclass: #Greeter.

Object subclass: #Shouts.
Shouts >> shout
  ^ 'HELLO'.

Object subclass: #Whispers.
Whispers >> whisper
  ^ 'goodbye'.

"Compose two independent behaviours into Greeter at runtime."
Greeter addBehavior: Shouts.
Greeter addBehavior: Whispers.

"The instance is created after composition, so it sees both behaviours."
g := Greeter new.
g shout , ' and ' , g whisper.
