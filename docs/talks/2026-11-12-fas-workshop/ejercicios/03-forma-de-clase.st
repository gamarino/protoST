"Ejercicio 3 — ¿Qué ve una instancia que ya existía cuando la clase cambia?

 En protoCore la cadena de padres de un objeto se fija al construirlo.
 Antes de correrlo, completen la tabla con true/false:

                         instancia vieja   instancia nueva
   método agregado antes
   de addBehavior:
   mixin de addBehavior:
   método agregado después
   de addBehavior:
   variable de instancia
   agregada redeclarando"

Object subclass: #Punto
  instanceVariableNames: 'x'

Punto >> x
  ^ x

Object subclass: #Describible

Describible >> describir
  ^ 'soy ', self printString

| vieja nueva |
vieja := Punto new.

Punto >> doble
  ^ 2

('vieja entiende #doble: ', (vieja respondsTo: #doble) printString) displayNl.

Punto addBehavior: Describible.
nueva := Punto new.
('nueva entiende #describir: ', (nueva respondsTo: #describir) printString) displayNl.
('vieja entiende #describir: ', (vieja respondsTo: #describir) printString) displayNl.
('vieja isKindOf: Punto: ', (vieja isKindOf: Punto) printString) displayNl.

Punto >> triple
  ^ 3

('nueva entiende #triple: ', (nueva respondsTo: #triple) printString) displayNl.
('vieja entiende #triple: ', (vieja respondsTo: #triple) printString) displayNl.

"Redeclarar una clase (sin addBehavior: de por medio) con otra variable de
 instancia, como haría un file-in de una versión nueva."
Object subclass: #Celda
  instanceVariableNames: 'a'

Celda >> a: v
  a := v

| c |
c := Celda new a: 1; yourself.

Object subclass: #Celda
  instanceVariableNames: 'a b'

Celda >> b
  ^ b

"En 0.7.0, si la línea siguiente empezara con '(', el parser la tomaba como
 argumento de un envío call-form b(...) dentro del método (corregido en main)."
Transcript showCr: 'la celda vieja entiende #b: ', (c respondsTo: #b) printString.
Transcript showCr: 'y su b vale: ', c b printString.
Transcript showCr: 'misma clase: ', (c class == Celda) printString.
