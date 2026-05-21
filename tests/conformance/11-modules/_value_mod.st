"-- a module exporting a Holder class — used by import-cached.st --"
Object subclass: #Holder.
Holder class >> compute
  ^ 2 + 3.
answer := Holder compute.
