"EXPECT: nil"
"§4.5: an instance variable never assigned reads as nil"
Object subclass: #Holder
  instanceVariableNames: 'slot'.
Holder >> slot
  ^ slot.
Holder new slot.
