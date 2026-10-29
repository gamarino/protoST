"Ejercicio 4 — Mirar al recolector mientras trabaja.

 Córranlo así, para ver una línea por ciclo de GC en stderr:
   PROTOCORE_HEAP_TRACE=1 protost 04-gc-en-vivo.st 2> traza.txt
 y después:
   grep -c 'protoCore heap' traza.txt          (cuántos ciclos)
   grep -o 'stw=[0-9.]*ms' traza.txt | sort -t= -k2 -n | tail -3   (las pausas más largas)
 En cada línea: L = celdas vivas al final del ciclo (de 64 bytes),
 S = límite blando, H = presupuesto, C = duración del ciclo concurrente,
 stw = la pausa stop-the-world.

 Antes de correrlo, anoten:
   a) ¿cuántas celdas vivas esperan al final? (2 000 Arrays de 8 retenidos)
   b) ¿la pausa crece con el tamaño del heap o con otra cosa?
 Después repitan con 4 actores asignando en paralelo (segunda mitad)
 y comparen C y stw."

Object subclass: #Asignador

Asignador >> asignar: n retener: cada
  | guardados |
  guardados := OrderedCollection new.
  1 to: n do: [:i | | tmp |
    tmp := Array new: 8.
    (i \\ cada) = 0 ifTrue: [ guardados add: tmp ] ].
  ^ guardados size

| t fs |
t := Time millisecondsToRun: [
  (Asignador new asignar: 500000 retener: 250) printNl ].
('~ un hilo: ', t printString, ' ms') displayNl.

t := Time millisecondsToRun: [
  fs := (1 to: 4) collect: [:i | Asignador new asActor asignar: 500000 retener: 250 ].
  ((Future whenAll: fs) wait inject: 0 into: [:a :b | a + b ]) printNl ].
('~ cuatro actores: ', t printString, ' ms') displayNl.
