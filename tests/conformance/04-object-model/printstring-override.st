"EXPECT: custom"
"§4.8: a class may override printString; ordinary inheritance applies"
Object subclass: #Widget.
Widget >> printString
  ^ 'custom'.
Widget new printString.
