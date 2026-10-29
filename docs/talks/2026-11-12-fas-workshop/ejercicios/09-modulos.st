"Ejercicio 9 — Módulos sin imagen.

 Córranlo desde esta carpeta (Import busca en el directorio actual, después
 en $STPATH, en el venv activo y en la biblioteca estándar).
 Antes de correrlo, anoten:
   a) ¿cuántas veces aparece '(cargando geometria.st)'?
   b) ¿los dos Import devuelven el mismo objeto?
   c) ¿se ve la clase privada _Ayudante desde afuera?
   d) ¿de qué clase es el área de un círculo de radio 2?"

| g g2 c json |
g := Import from: 'geometria'.
g2 := Import from: 'geometria'.
Transcript showCr: 'mismo módulo: ', (g == g2) printString.
c := g Circulo radio: 2.
Transcript showCr: 'área: ', c area printString, ' (', c area class name, ')'.
Transcript showCr: 'privada: ', ([ g _Ayudante. 'visible' ] on: Error do: [:e | 'no visible' ]).

"La biblioteca estándar se importa igual."
json := Import from: 'json'.
Transcript showCr: (json JSON stringify: (Dictionary new at: 'area' put: c area asFloat; yourself)).
