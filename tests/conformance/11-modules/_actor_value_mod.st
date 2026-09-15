"-- a module exporting a Doubler class — used by import-from-actor-method.st --"
Object subclass: #Doubler.
Doubler class >> of: n
  ^ n * 2.
