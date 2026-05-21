"EXPECT: an animal that barks"
"§4.6: super reuses inherited behaviour"
Object subclass: #Animal.
Animal >> describe
  ^ 'an animal'.
Animal subclass: #Dog.
Dog >> describe
  ^ (super describe) , ' that barks'.
Dog new describe.
