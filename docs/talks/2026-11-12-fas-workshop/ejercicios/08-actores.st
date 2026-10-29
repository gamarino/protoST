"Ejercicio 8 — La semántica de los actores, caso por caso.

 Antes de correrlo, anoten qué imprime cada número:
   1. ¿qué mensajes al proxy NO devuelven un Future?
   2. dos actores que se esperan mutuamente;
   3. el mismo intercambio encadenado con thenDo: en vez de wait;
   4. un actor que devuelve el Future de otro actor: ¿se aplana?
   5. un error dentro del actor, recibido por quien espera;
   6. el estado del actor después de ese error."

Object subclass: #Par
  instanceVariableNames: 'otro saldo'

Par >> initialize
  saldo := 100

Par >> otro: p
  otro := p

Par >> ping
  ^ otro pong wait

Par >> pong
  ^ otro valor wait

Par >> valor
  ^ 42

Par >> pingSinEsperar
  ^ otro valor

Par >> retirar: n
  saldo := saldo - n.
  n > 50 ifTrue: [ ^ self error: 'demasiado' ].
  ^ saldo

Par >> saldo
  ^ saldo

| a b f |
a := Par new asActor.
b := Par new asActor.
(a otro: b) wait.
(b otro: a) wait.

Transcript showCr: '1. ', a printString, ' / isNil: ', a isNil printString,
  ' / saldo: ', a saldo class name.
Transcript showCr: '2. ', ([ a ping wait ] on: Error do: [:e | e messageText copyFrom: 1 to: 9 ]).
f := Future new.
b valor thenDo: [:v | f resolve: v + 1 ].
Transcript showCr: '3. ', f wait printString.
Transcript showCr: '4. ', a pingSinEsperar wait printString.
Transcript showCr: '5. ', ([ (a retirar: 70) wait ] on: Error do: [:e | e class name, ': ', e messageText ]).
Transcript showCr: '6. saldo después del error: ', a saldo wait printString.
