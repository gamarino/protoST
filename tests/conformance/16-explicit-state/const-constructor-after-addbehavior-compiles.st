"EXPECT: #('user base:' 'Grown')"
"Spec §6.2, M9, Task 6 ruling m1: after addBehavior: the generated
 constructor of Grown is tag:base:, so base: is free for a user method. The
 compiler cannot see the composed constants: for a class this module
 composes with addBehavior:, M9 is judged when the method is installed."
Object subclass: #Grown instanceVariableNames: '' constantNames: 'base'.
Object subclass: #Tagged instanceVariableNames: '' constantNames: 'tag'.
Grown addBehavior: Tagged.
Grown class >> base: b
  ^ 'user base:'

{Grown base: 1. (Grown tag: 1 base: 2) class name}
