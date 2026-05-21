"EXPECT: an animal"
"§4.2: an instance delegates an undefined method to its class prototype chain"
Object subclass: #Animal.
Animal >> describe
  ^ 'an animal'.
Animal subclass: #Dog.
Dog new describe.
