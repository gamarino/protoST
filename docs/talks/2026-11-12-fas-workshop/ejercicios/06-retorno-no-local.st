"Ejercicio 6 — Retorno no local sin contextos reificados.

 En protoST los frames no son objetos: son registros de enteros sobre un
 arreglo de slots por hilo. Saber si el 'home' de un bloque sigue vivo se
 resuelve con un registro thread-local de engines vivos.
 Antes de correrlo, anoten qué pasa en cada caso:
   1. ^ dentro de un bloque que corre la primitiva do:
   2. ^ desde un bloque cuyo método ya retornó
   3. ensure: cuando el bloque protegido sale por ^
   4. ^ desde un bloque que se manda a OTRO actor, mientras el método
      home sigue vivo, esperando la respuesta."

Object subclass: #Buscador

Buscador >> primeroMayorQue: n en: coll
  coll do: [:e | e > n ifTrue: [ ^ e ] ].
  ^ nil

Buscador >> fabricarEscape
  ^ [:x | ^ x * 10 ]

Buscador >> conLimpieza
  [ #(1 2 3) do: [:i | i = 2 ifTrue: [ ^ i ] ] ]
    ensure: [ Transcript showCr: '3. ensure corrió' ].
  ^ 0

Buscador >> desdeOtroActor
  | otro |
  otro := Ejecutor new asActor.
  ^ (otro correr: [ ^ #desdeElBloque ]) wait

Object subclass: #Ejecutor

Ejecutor >> correr: unBloque
  ^ unBloque value

| b escape |
b := Buscador new.
Transcript showCr: '1. ', (b primeroMayorQue: 3 en: #(1 2 5 7)) printString.
escape := b fabricarEscape.
Transcript showCr: '2. ', ([ escape value: 4 ] on: Error do: [:e | e class name ]).
Transcript showCr: '3. retornó ', b conLimpieza printString.
[ b desdeOtroActor ] on: Error do: [:e |
  Transcript showCr: '4. ', e class name.
  "El texto cambió después de 0.7.0: ¿qué dice el suyo, y es verdad?"
  Transcript showCr: '~ 4. ', e messageText ].
