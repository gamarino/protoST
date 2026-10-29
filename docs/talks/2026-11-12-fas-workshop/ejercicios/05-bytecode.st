"Ejercicio 5 — Leer el bytecode.

 Antes de correr  protost --disasm 05-bytecode.st  (o de leer
 docs/vm-examples/*.txt si su protost no tiene --disasm), predigan para
 cada método:
   a) ¿qué variables viven en el diccionario de capturadas
      (MAKE_CAPTURED / PUSH_CAPTURED) y cuáles en el frame (PUSH_LOCAL)?
   b) ¿qué bloques se inlinean (no aparece PUSH_BLOCK) y cuáles no?
   c) ¿dónde aparecen BIN_INT_ADD, IVAR_GROUP y ASSERT_BOOL_OR_DNU?
   d) en #contadores: ¿los tres bloques comparten una variable o cada uno
      tiene la suya?"

Object subclass: #Muestra
  instanceVariableNames: 'x y z'

Muestra >> sumaHasta: n
  | s |
  s := 0.
  1 to: n do: [:i | s := s + i ].
  ^ s

Muestra >> sumaConBloque: n
  | s |
  s := 0.
  (1 to: n) do: [:i | s := s + i ].
  ^ s

Muestra >> x: ax y: ay
  x := ax.
  y := ay.
  z := 0

Muestra >> signo: n
  ^ n > 0 ifTrue: [ 1 ] ifFalse: [ n < 0 ifTrue: [ -1 ] ifFalse: [ 0 ] ]

Muestra >> contadores
  ^ (1 to: 3) collect: [:i | | c | c := i * 10. [ c := c + 1 ] ]

| m bloques |
m := Muestra new.
(m sumaHasta: 10) printNl.
(m sumaConBloque: 10) printNl.
(m x: 1 y: 2) printNl.
(m signo: -5) printNl.
bloques := m contadores.
bloques first value.
bloques first value.
(bloques collect: [:b | b value ]) printNl.
