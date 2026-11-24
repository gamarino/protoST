"-- used by const-constructor-stale-after-redeclaration.st: Sub inherits
 the constructor number: from its redeclared superclass (M9)."
Sub class >> number: n
  ^ 'user number: on Sub'
