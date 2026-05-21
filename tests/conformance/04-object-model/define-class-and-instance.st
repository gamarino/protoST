"EXPECT: 2"
"§4.3-4.4: define a class, create an instance, drive instance methods"
Object subclass: #Counter
  instanceVariableNames: 'value'.
Counter >> initialize
  value := 0.
Counter >> increment
  value := value + 1.
Counter >> value
  ^ value.
c := Counter new.
c initialize.
c increment.
c increment.
c value.
