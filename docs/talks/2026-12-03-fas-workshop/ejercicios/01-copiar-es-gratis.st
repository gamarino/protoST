"Ejercicio 1 — Identidad, estado y valor.

 Un Array de protoST es un objeto mutable (una identidad) que apunta a una
 instantánea inmutable (un valor: una lista persistente de protoCore).
 Antes de correrlo, anoten:
   a) ¿cuánto tarda copiar un Array de un millón de elementos?
   b) ¿cuánto tardan 100 000 at:put: sobre la copia?
   c) ¿qué imprime la línea del alias?
   d) ¿qué pasa con el String?
 Las líneas que empiezan con ~ son mediciones: varían de una máquina a otra."

| big copia alias t1 t2 |
big := (1 to: 1000000) asArray.
t1 := Time millisecondsToRun: [ copia := big copy ].
t2 := Time millisecondsToRun: [ 1 to: 100000 do: [:i | copia at: i put: 0 ] ].
('~ copy de 1M elementos: ', t1 printString, ' ms') displayNl.
('~ 100 000 at:put: sobre la copia: ', t2 printString, ' ms') displayNl.
('original intacto: ', ((big at: 1) = 1) printString) displayNl.
('copia modificada: ', ((copia at: 1) = 0) printString) displayNl.

"Un alias es la misma identidad: ve el cambio."
alias := big.
big at: 1 put: 42.
('el alias ve el cambio: ', (alias at: 1) printString) displayNl.

"Un String no tiene identidad mutable detrás: es el valor mismo."
[ 'abc' copy at: 1 put: $x ]
  on: Error
  do: [:e | ('String: ', e class name) displayNl ].
('a', 'bc') displayNl.
