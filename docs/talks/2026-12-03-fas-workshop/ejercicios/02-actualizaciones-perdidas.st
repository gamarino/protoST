"Ejercicio 2 — Ninguna escritura se rompe, pero un incremento se puede perder.

 Ocho actores incrementan un mismo contador. Hay cuatro versiones:
   1. el contador es un objeto común, compartido por referencia;
   2. el contador es un actor;
   3. el total es un Atom (compare-and-swap con reintento);
   4. el contador es un objeto común con una variable de instancia atomic
      (protoST 0.17.0): update:with: es el mismo reintento, sobre el atributo.
 Antes de correrlo, anoten para cada versión: ¿cuánto da? ¿siempre lo mismo?
 ¿Puede quedar el contador en un estado 'roto' (ni el valor viejo ni el nuevo)?"

Object subclass: #Contador
  instanceVariableNames: 'n'

Contador >> initialize
  n := 0

Contador >> incr
  n := n + 1

Contador >> n
  ^ n

Object subclass: #Obrero

Obrero >> golpear: unContador veces: k
  1 to: k do: [:i | unContador incr ].
  ^ k

Obrero >> sumarEn: unAtom veces: k
  1 to: k do: [:i | unAtom swap: [:v | v + 1 ] ].
  ^ k

Object subclass: #ContadorAtomico
  instanceVariableNames: ''
  atomicNames: 'n'

ContadorAtomico >> initialize
  self compareAndSet: #n from: nil to: 0

ContadorAtomico >> incr
  ^ self update: #n with: [:v | v + 1 ]

ContadorAtomico >> n
  ^ n

Obrero >> golpearAtomico: unContador veces: k
  1 to: k do: [:i | unContador incr ].
  ^ k

| comun actor atom fs atomico |
comun := Contador new.
fs := (1 to: 8) collect: [:i | Obrero new asActor golpear: comun veces: 100000 ].
(Future whenAll: fs) wait.
('~ 1. objeto compartido: ', comun n printString, ' de 800000') displayNl.
('~ 1. ¿se perdieron incrementos? ', (comun n < 800000) printString) displayNl.

actor := Contador new asActor.
fs := (1 to: 8) collect: [:i | Obrero new asActor golpear: actor veces: 10000 ].
(Future whenAll: fs) wait.
('2. actor: ', actor n wait printString, ' de 80000') displayNl.

atom := Atom on: 0.
fs := (1 to: 8) collect: [:i | Obrero new asActor sumarEn: atom veces: 10000 ].
(Future whenAll: fs) wait.
('3. Atom: ', atom value printString, ' de 80000') displayNl.

atomico := ContadorAtomico new.
fs := (1 to: 8) collect: [:i | Obrero new asActor golpearAtomico: atomico veces: 10000 ].
(Future whenAll: fs) wait.
('4. variable atomic: ', atomico n printString, ' de 80000') displayNl.
