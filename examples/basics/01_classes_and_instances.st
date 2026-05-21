"EXPECT: 2"
"-- 01_classes_and_instances.st --------------------------------------------"
"-- The object model, step one: declare a class with an instance variable, --"
"-- define instance-side methods, create an instance, send it messages.    --"
"--                                                                        --"
"-- Note: `new` is the raw allocator — it does NOT auto-call `initialize`   --"
"-- (STATUS.md D4), so the caller sends `initialize` explicitly.            --"
"-- Expected output: 2 (the counter after two increments).                 --"

Object subclass: #Counter instanceVariableNames: 'value'.

Counter >> initialize
  value := 0.
  ^ self.

Counter >> increment
  value := value + 1.
  ^ self.

Counter >> value
  ^ value.

c := Counter new.
c initialize.
c increment.
c increment.
c value.
