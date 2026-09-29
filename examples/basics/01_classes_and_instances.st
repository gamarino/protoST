"EXPECT: 2"
"-- 01_classes_and_instances.st --------------------------------------------"
"-- The object model, step one: declare a class with an instance variable, --"
"-- define instance-side methods, create an instance, send it messages.    --"
"--                                                                        --"
"-- `new` sends `initialize` to the fresh instance, as in Pharo.           --"
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
c increment.
c increment.
c value displayNl.
