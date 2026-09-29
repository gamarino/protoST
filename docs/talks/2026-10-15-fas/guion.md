# protoST — guion de la charla (FAST, 15/10/2026)

Duración: núcleo de 30 minutos; módulos opcionales hasta 60. Todas las cifras
salen de `benchmarks/reports/2026-09-29-release-0.4.0.md`; si una cifra no
está en ese informe, no se dice.

Antes de empezar: terminal con fuente grande, `cd docs/talks/2026-10-15-fas/demos`,
`protost --version` debe decir `protoST 0.4.1`. Las grabaciones de respaldo
están en `../recordings/` (ver la checklist).

---

## Núcleo (30 minutos)

### 0:00 — Quién y por qué (2 min)

- Esto es un experimento que busca entusiastas, no un producto que busca
  usuarios.
- protoST no es un Smalltalk mejor y no compite con el entorno vivo. Es otra
  cosa: poder leer y escribir el Smalltalk que conocen en lugares donde hoy
  Smalltalk no llega.
- Lo hace una persona con agentes de IA como herramientas; todo lo que muestro
  está en el repositorio y se puede verificar.

### 0:02 — Preguntas, no respuestas (4 min)

Planteadas como preguntas al público (no afirmarlas por ellos):

- ¿Cuánto les cuesta hoy poner una imagen como servicio detrás de otra cosa?
- ¿Cuántos núcleos usa su código Smalltalk cuando la máquina tiene doce?
- ¿Cómo integran lo que escriben en Smalltalk con el resto del stack
  (Python, JavaScript, colas, bases)?
- ¿Cómo comparten código como texto plano con quien no usa una imagen?

No decimos que protoST resuelve todo eso. Decimos que abre una forma distinta
de intentarlo.

### 0:06 — Por qué ahora es posible: protoCore (5 min)

- protoCore: un núcleo de objetos en C++ sobre el que corren varios lenguajes
  (JavaScript, Python, Clojure, Scala, protoST).
- Tres propiedades, cada una con un ejemplo simple:
  - estructuras inmutables compartidas por estructura: una lista "modificada"
    es una nueva versión que comparte casi todo con la anterior;
  - hilos nativos sin lock global y un GC concurrente;
  - un objeto mutable es una referencia atómica a una instantánea inmutable:
    cada lectura ve una instantánea consistente.
- Desde Smalltalk los objetos siguen cambiando: `at:put:` en un `Array` o
  `add:` en una `OrderedCollection` modifican el objeto instalando una
  instantánea nueva. Los strings no: un String es un `ProtoString` de
  protoCore (una cuerda, un valor sin modificación en el lugar).
- Consecuencia práctica: pasar un objeto a otro hilo es pasar una referencia,
  sin copiarlo. El objeto queda compartido: lo que un lado cambia, el otro lo
  ve en su próxima lectura.

### 0:11 — DEMO 1: el código que ya conocen (5 min)

`./run_demo.sh 01-familiar-code.st`

Mostrar el archivo antes de correrlo (clases, `printOn:`, bloques, retorno
no local en `Bank>>find:`, excepción propia con `retry`, `ZeroDivide` con
`resume: 0`, fracciones exactas, `LargeInteger`). Puntos a remarcar:

- es un archivo de texto; una línea en blanco cierra un método;
- `new` envía `initialize`, `printOn:` gobierna cómo se imprime todo;
- la última línea imprime el total (270): un script muestra solo lo que imprime.

Respaldo: `scriptreplay` de `recordings/01-familiar-code`.

### 0:16 — Actores y paralelismo real (3 min)

- Cualquier objeto se vuelve actor con `asActor`; cada mensaje devuelve un
  `Future` enseguida (salvo los que preguntan por la referencia misma:
  identidad, igualdad, `hash`, `isNil`, `isActor`, `printString`, que el
  proxy responde en el acto); el actor procesa un mensaje por vez, así su
  estado son variables de instancia comunes, sin locks.
- Los actores comparten un pool de hilos nativos.
- `wait`, `thenDo:`, `whenAll:`. Un error dentro del actor vuelve con su
  clase al que espera.

### 0:19 — DEMO 2: el mismo cálculo en un hilo y en doce actores (3 min)

`./run_demo.sh 02-actors-parallelism.st`

Los dos resultados coinciden (4946 primos) y se muestran los tiempos
medidos en ese momento. Decir la aceleración que aparezca en pantalla, no
una de memoria. Remarcar: el código del cálculo no cambió; cambió quién lo
ejecuta.

Respaldo: `recordings/02-actors-parallelism`.

### 0:22 — Mundos que podría abrir (4 min)

Ideas, no promesas:

- **Gemelos digitales**: un actor por componente físico; cada evento se
  procesa atómicamente; los componentes conversan por mensajes. (Si hay
  tiempo, DEMO 3.)
- **Servicios**: un proceso que arranca en decenas de milisegundos y lee
  archivos, sin imagen que desplegar.
- **Convivir con otros runtimes**: es la dirección del proyecto. Hoy un
  programa protoScala importa un módulo protoST y recibe el mismo objeto, sin
  copia; todavía no puede usarlo. Decirlo así.

### 0:26 — Lo que protoST no es (2 min)

Leer los titulares del capítulo 14 del tutorial: no hay imagen ni browser,
una línea en blanco cierra un método, strings inmutables, símbolos cortos
iguales a strings, metaclase delgada, `thisContext` no soportado, un actor que
espera no atiende otros mensajes (y un ciclo de esperas se reporta como
error). Mostrar que está todo escrito y verificado.

### 0:28 — Cómo participar (2 min)

- Repositorio, tutorial (el capítulo 14 es para ustedes), `docs/STATUS.md`
  con lo abierto.
- Lo que más ayuda: programas reales escritos como los escribirían ustedes, y
  reportes de todo lo que sorprenda.
- Preguntas.

---

## Módulos (hasta 60 minutos)

### M1 — protoCore por dentro (10 min)

Celdas de 64 bytes, enteros inline en el puntero, listas AVL y cuerdas
(ropes) inmutables, el GC concurrente que difiere la recolección hasta que es
necesaria, `ProtoThread` y cómo protoST la usa para los workers. La
instantánea atómica de un objeto mutable (dibujo en pizarra).

### M2 — El gemelo digital paso a paso (8 min)

`./run_demo.sh 03-digital-twin.st`: la bomba como máquina de estados en un
actor, tres sensores con 50 ms de E/S simulada leídos en paralelo en cada
ciclo (el tiempo por ciclo en pantalla ronda los 50 ms, no 150), la alarma
que detiene la bomba al pasar 75 grados. Recorrer el código.

Respaldo: `recordings/03-digital-twin`.

### M3 — Rendimiento y método (6 min)

- Cada benchmark verifica su resultado; el harness falla si falta o no
  coincide (se mostró un bug que se veía como "mejora" en otro proyecto por
  no verificar).
- Cifras del informe 0.4.0 (`benchmarks/reports/2026-09-29-release-0.4.0.md`):
  - arranque 27 ms (CPython 29 ms);
  - proceso completo frente a CPython con el mismo N: media geométrica 3,45×,
    dominada por el arranque;
  - solo el trabajo (arranque descontado): de unas 6× (bucle de enteros) a
    36× (`fib`) y 64× (excepciones), media geométrica 20,8×;
  - escalado de `saturation_big`: techo de unas 2,1× con cuatro workers,
    confirmado por una segunda corrida con menos carga; doce actores, 2,1×
    con el pool.
- Decir dónde es más lento (despacho de métodos recursivo, excepciones) sin
  maquillarlo.

### M4 — Hoja de ruta y preguntas abiertas (6 min)

Interoperabilidad real entre runtimes (requiere trabajo en protoCore),
`thisContext`, herramientas (debugger DAP existe), rendimiento de envío de
mensajes. Preguntar al público qué les resultaría útil primero.
