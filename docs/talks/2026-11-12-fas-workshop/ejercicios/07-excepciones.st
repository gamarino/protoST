"Ejercicio 7 — Excepciones: buscar el handler sin desenrollar.

 signal recorre una pila de handlers thread-local y corre el handler ENCIMA
 de los frames que señalaron; recién return:, retry o caer al final del
 handler desenrollan (con excepciones de C++ como transporte).
 Antes de correrlo, anoten el orden exacto de las líneas impresas."

Error subclass: #SinFondos

| log intentos r |
log := OrderedCollection new.

"resume: el bloque protegido continúa en el punto del signal."
r := [ log add: 'antes'. log add: (Warning signal: 'w'). log add: 'después'. #fin ]
  on: Warning do: [:e | log add: 'handler'. e resume: 'resumido' ].
log add: r.

"retry: vuelve a correr el bloque protegido entero."
intentos := 0.
r := [ intentos := intentos + 1.
       intentos < 3 ifTrue: [ SinFondos signal: 'falta' ].
       intentos ]
  on: SinFondos do: [:e | log add: 'reintento ', intentos printString. e retry ].
log add: r.

"ensure: e ifCurtailed: mientras se desenrolla hacia el handler."
r := [ [ [ 1 / 0 ] ifCurtailed: [ log add: 'curtailed' ] ]
         ensure: [ log add: 'ensure' ] ]
  on: ZeroDivide do: [:e | log add: 'handler ZeroDivide'. -1 ].
log add: r.

"ZeroDivide es reanudable."
log add: ([ (1 / 0) + 1 ] on: ZeroDivide do: [:e | e resume: 41 ]).

"outer: en Pharo el handler interno continúa; en protoST 0.7.0 se comporta
 como pass (D7). ¿Qué imprime?"
r := [[ Warning signal: 'w'. 'cuerpo' ]
        on: Warning do: [:e | e outer. 'interno continuó' ]]
      on: Warning do: [:e | e resume: 5 ].
log add: r.

log do: [:each | Transcript showCr: each printString ].
