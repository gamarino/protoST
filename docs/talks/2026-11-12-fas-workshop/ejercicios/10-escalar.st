"Ejercicio 10 — ¿Cuánto escala, y por qué no más?

 El mismo cálculo (contar primos hasta 120 000) en un hilo y repartido en
 12 actores. Córranlo varias veces cambiando el tamaño del pool:
   PROTOST_WORKERS=1 protost 10-escalar.st
   PROTOST_WORKERS=2 protost 10-escalar.st
   PROTOST_WORKERS=4 protost 10-escalar.st
   protost 10-escalar.st            (por omisión: los núcleos, hasta 8)
 Anoten la aceleración de cada corrida. Antes de medir, predigan:
   a) ¿la curva es lineal hasta el número de núcleos físicos?
   b) ¿qué cambia si el cálculo asigna memoria en cada paso?
      (comparen con el ejercicio 4: allí cuatro actores asignando
      tardan bastante más que un cuarto del tiempo de cuatro hilos.)"

Object subclass: #Criba

Criba >> esPrimo: n
  | d |
  n < 2 ifTrue: [ ^ false ].
  d := 2.
  [ d * d <= n ] whileTrue: [
    (n \\ d) = 0 ifTrue: [ ^ false ].
    d := d + 1 ].
  ^ true

Criba >> contarDesde: a hasta: b
  | c |
  c := 0.
  a to: b do: [:n | (self esPrimo: n) ifTrue: [ c := c + 1 ] ].
  ^ c

| tope partes t1 t2 r1 r2 paso fs |
tope := 120000.
partes := 12.
t1 := Time millisecondsToRun: [ r1 := Criba new contarDesde: 1 hasta: tope ].
paso := tope // partes.
t2 := Time millisecondsToRun: [
  fs := (0 to: partes - 1) collect: [:i |
    Criba new asActor contarDesde: i * paso + 1 hasta: (i + 1) * paso ].
  r2 := (Future whenAll: fs) wait inject: 0 into: [:x :y | x + y ] ].
Transcript showCr: 'primos: ', r1 printString, ' y ', r2 printString.
Transcript showCr: 'coinciden: ', (r1 = r2) printString.
Transcript showCr: '~ un hilo: ', t1 printString, ' ms; ', partes printString, ' actores: ', t2 printString, ' ms'.
Transcript showCr: '~ aceleración: ', (t1 * 10 // (t2 max: 1) / 10.0) printString, 'x'.
