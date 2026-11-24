"-- used by const-constructor-user-method-redeclared.st: a class with a
 user class method number:, declared before the class has constants."
Object subclass: #Account instanceVariableNames: 'x'.
Account class >> number: n
  ^ 'user number:'
