"EXPECT: 5"
"§7: a ^ written directly in a method body is an ordinary method return"
Object subclass: #Plain.
Plain >> five
  ^ 5.
Plain new five.
