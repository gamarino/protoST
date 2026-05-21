"EXPECT: an animal that barks loudly"
"-- 02_inheritance_and_super.st --------------------------------------------"
"-- Inheritance and `super`: a subclass overrides a method and reuses the   --"
"-- inherited implementation by sending `super`. The chain is two levels    --"
"-- deep (Animal -> Dog -> LoudDog), and each `super describe` reaches the  --"
"-- next class up.                                                          --"
"-- Expected output: an animal that barks loudly                            --"

Object subclass: #Animal.
Animal >> describe
  ^ 'an animal'.

Animal subclass: #Dog.
Dog >> describe
  ^ (super describe) , ' that barks'.

Dog subclass: #LoudDog.
LoudDog >> describe
  ^ (super describe) , ' loudly'.

LoudDog new describe.
