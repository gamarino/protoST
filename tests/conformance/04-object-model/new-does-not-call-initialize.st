"EXPECT: nil"
"§4.4 + D4: new does NOT auto-invoke initialize, so value stays nil"
Object subclass: #Counter
  instanceVariableNames: 'value'.
Counter >> initialize
  value := 0.
Counter >> value
  ^ value.
Counter new value.
