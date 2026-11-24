# protoST por dentro — guía de trabajo

Fundación Argentina de Smalltalk · 3/12/2026 · Gustavo Marino

## Para qué es esta guía

Esto no es un curso. protoST es un demostrador de protoCore: un Smalltalk sobre un núcleo donde todo valor es inmutable, con hilos nativos y sin lock global, pensado como base para gemelos digitales. No es un clon de Smalltalk ni pretende ser "un Smalltalk mejor". Lo que les propongo es otra cosa: **su código Smalltalk donde Smalltalk hoy no llega** (todos los núcleos sin lock global, un archivo que corre como un comando, una base de datos escrita en Smalltalk, los mismos objetos que ve otro lenguaje). No sé hasta dónde puede llegar.

Los ejercicios están pensados para que cada uno ponga a prueba una decisión de diseño con sus propias manos, le vea el costo y saque su conclusión. Lo que más me sirve de ustedes es el criterio de quien construyó VMs de Smalltalk: dónde esto les parece prometedor, dónde les parece un error y qué investigarían. Para aprender el lenguaje está `docs/tutorial/`, en especial el capítulo 14, escrito para quien ya sabe Smalltalk-80.

Cada ejercicio tiene la misma forma: una **pregunta**, un **programa de partida** que corre tal cual, una **predicción** que les pedimos anotar antes de correrlo y una **discusión** con lo que hay detrás en el código de protoST o de protoCore. Los programas están en [`ejercicios/`](ejercicios/), con la salida esperada al lado. Las slides están en [`deck.html`](deck.html) y en [`slides/`](slides/) en PDF.

## La jornada: dos sesiones

El taller tiene dos mitades de duración parecida. Esta guía cubre la primera, protoST. La segunda es **protoBaseST**, una base de datos de objetos perenne escrita en protoST, con su propio deck y su propio menú de proyectos para voluntarios:

- deck y materiales de protoBaseST: <https://github.com/gamarino/protoBaseST/tree/main/docs/talks/2026-12-03-fas-workshop>
- proyectos voluntarios: <https://github.com/gamarino/protoBaseST/blob/main/docs/talks/2026-12-03-fas-workshop/proyectos-voluntarios.md>

| Hora (3 h) | Hora (2 h) | Bloque | Qué se hace |
| --- | --- | --- | --- |
| 0:00 | 0:00 | Apertura | La apuesta, el posicionamiento, qué cambió desde noviembre |
| 0:10 | 0:05 | protoST I — el modelo | Identidad, estado y valor; formas compartidas; estructuras persistentes; GC sin write barrier |
| 0:30 | — | Laboratorio 1 | Ejercicios 1 y 2 (3 y 4 quedan para casa) |
| 0:45 | 0:20 | protoST II — la VM y los actores | Bytecode, excepciones, actores, la lección del actor enterrado |
| 1:05 | 0:35 | Laboratorio 2 | 3 h: ejercicios 7, 8 y 10. 2 h: solo el 2 y el 8 (5, 6 y 9 quedan para casa) |
| 1:20 | 0:45 | Dirección | Estado explícito: los modificadores salieron en 0.17.0; las clases valor son una propuesta para 0.18.0 |
| 1:30 | 0:55 | Corte | 10 minutos (5 en la versión de 2 h) |
| 1:40 | 1:00 | protoBaseST | La segunda sesión, con su propio deck |
| 2:50 | 1:50 | Cierre común | Proyectos voluntarios de las dos sesiones, preguntas |

Trabajen de a dos si pueden: uno predice, el otro corre. Lo que no termine en la sala queda como tarea; la última sección propone líneas para seguir.

## Preparación (antes del 3/12)

Instalen antes de venir: en la sala no hay tiempo para compilar. Necesitan **protoST 0.17.0 sobre protoCore 3.6.1**. La sesión de protoBaseST pide la misma versión de protoST.

**Ubuntu 24.04 o Windows con WSL2 (recomendado).** Son dos paquetes `.deb` de las releases de GitHub:

```bash
# Windows, una sola vez, en PowerShell:
#   wsl --install -d Ubuntu-24.04
wget https://github.com/numaes/protoCore/releases/download/v3.6.1/protoCore-3.6.1-Linux.deb
wget https://github.com/gamarino/protoST/releases/download/v0.17.0/protost-0.17.0-Linux.deb
sudo apt install ./protoCore-3.6.1-Linux.deb ./protost-0.17.0-Linux.deb
protost --version          # protoST 0.17.0
```

Si ya tenían una versión anterior, el mismo `apt install` actualiza las dos: 0.17.0 necesita protoCore 3.6.1 (desde 0.13.0) y el `.deb` lo declara como dependencia.

**macOS y Windows nativo.** No hay paquete publicado: se compila desde las fuentes (C++20, CMake 3.20+, protoCore 3.6.1 instalado, protoIO 0.7.1 o un 0.7.x posterior, OpenSSL; en macOS también readline). Los pasos están en `docs/INSTALLATION.md`, incluida la sección *Windows (MSVC)*. Cuenten con una hora la primera vez.

**El repositorio.** Clonen `https://github.com/gamarino/protoST` y usen los ejercicios de `docs/talks/2026-12-03-fas-workshop/ejercicios/` en `main`. El paquete 0.17.0 instala una copia anterior en `/usr/share/protoST/talks/2026-11-12-fas-workshop/`: con 0.17.0 su ejercicio 7 da una línea distinta de la esperada, porque `outer` cambió en 0.10.0 (ver el ejercicio 7).

**Verificación en dos minutos.**

1. `protost -e "(1/3) + (2/3)"` responde `1`.
2. `protost -e "100 factorial printString size"` responde `158`.
3. `protost -e "Float fsum: #(1.0e100 1.0 -1.0e100)"` responde `1.0` (la suma ingenua da `0.0`; ver "Lo nuevo").
4. `protost -i` abre el REPL; salen con Ctrl-D.
5. `protost --disasm docs/vm-examples/05-fib.st` imprime el bytecode y después corre el programa. Los mismos listados, ya generados, están en `docs/vm-examples/*.txt`; un test verifica que no se desactualicen.
6. En la carpeta de ejercicios, `./verificar.sh` corre los diez programas y compara con la salida esperada; ninguno tarda más de unos segundos. Con 0.17.0 tiene que terminar en `todos los ejercicios dan lo esperado`.

**Opcional: el depurador.** La extensión de VS Code (`editor-integration/vscode/`) habla DAP con `protost --dap`: breakpoints, paso a paso, excepciones y varios hilos detenidos a la vez cuando hay actores. Ver `docs/debugging.md`.

**Máquina.** Cualquier x86-64 con 4 núcleos o más sirve. Las mediciones de paralelismo dependen de la carga: cierren lo que puedan y, en una notebook, conéctenla a la corriente.

**Las cifras de esta guía.** Las mediciones marcadas "medido el 23/11" son corridas de 0.16.1 (protoCore 3.6.1) en una notebook Ryzen 5 5500U (6 núcleos, 12 hilos) que **no estaba en reposo**: corrían otras tareas (carga de 3 a 4). Son órdenes de magnitud, no curvas. Las demás cifras citan su fuente.

## Lo nuevo desde el taller de noviembre (0.9.0 → 0.17.0)

Todo esto está en `CHANGELOG.md` y `docs/STATUS.md`; acá va lo que cambia algo de lo que van a ver.

- **0.10.0, el trabajo de base para protoBaseST.** `UUID`, `Decimal` y valores de calendario escritos en protoST; el `hash` numérico es el de CPython; `ByteArray randomBytes:`. Excepciones con la semántica de Pharo: `outer` (D7, cerrado), `pass`, `retryUsing:` e `isResumable`. Una metaclase de verdad: `Foo class class` es `Metaclass`, existen `Behavior` y `Class`, y una clase ya no responde los métodos de sus instancias (D52). En la VM: atajos enteros para `*`, `\\`, `//`, `bitAnd:` y `bitOr:`; `==` y los tests de `nil` nunca envían; los bloques de control con temporales se inlinean; una primitiva es un método nativo de protoCore.
- **0.13.0, formas compartidas.** Cada clase guarda una instancia plantilla inmutable cuya `ProtoShape` (protoCore 3.6.1) nombra todas sus variables de instancia; `new` es un clon de la plantilla, así que todas las instancias de una clase comparten una forma. protoST pasa a usar solo la API pública de protoCore (un test lo verifica). El costo, medido y publicado: unas 60 instrucciones más por envío (+6,9 %), y a cambio `Array new:` y las clausuras un 14 % menos.
- **0.14.0.** Comparaciones exactas de `Fraction` en una primitiva (de unos 11 µs a 1 µs por comparación en el micro-benchmark de `CHANGELOG.md`).
- **0.15.0 y 0.16.0, sumas correctamente redondeadas.** `Float fsum:` es un port línea por línea de `math.fsum` de CPython (parciales de Shewchuk), comparado bit a bit contra 237 casos generados con CPython 3.14. `fsumOfProducts:with:`, `fsumOfSquaredDifferences:with:` y `fsumOfPowSquaredDifferences:with:` existen para los índices vectoriales de protoBaseST. La última sigue al `pow` de la plataforma, así que su último bit puede diferir entre glibc y MSVC: es la desviación D58, y vuelve a aparecer en la sección "Dirección".
- **0.14.1 a 0.16.1, el planificador de actores.** Cuando un actor espera dentro de un bloque que corre una primitiva, su worker "ayuda": corre mensajes de otros actores encima de la espera. Cinco arreglos en ese mecanismo (S40, S43, S45 a S48), todos encontrados por programas reales o por la revisión de un arreglo anterior. El último, S48, es la lección del ejercicio 8.
- **0.17.0, estado explícito I.** Modificadores de variables de instancia (`constantNames:`, `writeOnceNames:`, `atomicNames:`) con `update:with:` y `compareAndSet:from:to:` (ver "Dirección"). Cambio de nombre: el CAS sobre una variable es `compareAndSet:from:to:`, y el viejo nombre queda deprecado (ver "Dirección").
- **Tests.** 1631 casos de `ctest` en Linux, todos pasando con 0.17.0 (`docs/STATUS.md`); los del grupo `tsan` también bajo ThreadSanitizer. Son programas propios: no hay suite ANSI.

## Laboratorio 1 — Identidad, estado y valor

Los dos primeros ejercicios ponen a prueba la decisión central: en protoCore todo valor es inmutable, y un objeto "mutable" es una identidad cuyo estado actual es una instantánea inmutable. Escribir es publicar una instantánea nueva con un compare-and-swap. Anoten la predicción antes de correr y antes de leer la discusión.

### Ejercicio 1 — Copiar es gratis (10 min)

`protost 01-copiar-es-gratis.st`

**Pregunta.** ¿Cuánto cuesta `copy` sobre un `Array` de un millón de elementos, y cuánto cuesta después cada `at:put:`?

**Discusión.** El `copy` es O(1): la copia es otra identidad que apunta a la misma lista persistente (medido el 23/11: 0 ms). Cada `at:put:` copia un camino del árbol y publica la raíz nueva: 100 000 de ellos tardaron entre 91 y 98 ms, menos de 1 µs cada uno. Desde protoCore 3.6.0 la lista es un árbol RRB (*relaxed radix balanced*, el de `Vector` de Scala) con seis elementos por celda de 64 bytes, en lugar del AVL con un elemento por nodo (CHANGELOG de protoST 0.9.1). `KNOWN_ISSUES.md` K5 describe el costo de construir listas grandes en masa (unas n·log n celdas transitorias): se midió con el AVL y no lo volví a medir con el RRB.

El `String` no tiene identidad mutable detrás: es el valor mismo (una cuerda de protoCore), por eso `at:put:` responde `ModificationForbidden` (D34).

**Para discutir.** ¿Qué idioms de Smalltalk dependen de que `copy` sea caro y `at:put:` barato? ¿Qué se rompe en un `WriteStream` que crece de a un carácter?

### Ejercicio 2 — Ninguna escritura se rompe, pero un incremento se puede perder (15 min)

`protost 02-actualizaciones-perdidas.st`

**Pregunta.** Ocho actores incrementan un mismo contador 100 000 veces cada uno. ¿Cuánto da con un objeto común compartido, con el contador como actor, con un `Atom` y (0.17.0) con una variable de instancia `atomic`?

**Discusión.** Con el objeto común se pierde la mayoría de los incrementos (medido el 23/11, tres corridas: 190 207, 183 468 y 215 498 de 800 000), pero el objeto nunca queda en un estado roto. Cada escritura de variable de instancia es atómica: el CAS sobre la tabla de mutables reintenta hasta publicar. Lo que no es atómico es `n := n + 1`, que son dos operaciones: leer y publicar. Gana la última escritura.

Las dos correcciones son las dos herramientas del modelo:

- el **actor** serializa la lógica: un mensaje por vez;
- el **Atom** es una celda con compare-and-swap; `swap:` reintenta el bloque, que debe ser puro;
- desde 0.17.0, una **variable de instancia `atomic`**: el mismo reintento sobre el atributo (`self update: #n with: [:v | v + 1]`). Es la cuarta versión del ejercicio y también da 80 000 de 80 000.

Una tercera, menos visible: una racha de asignaciones a variables de instancia se publica como una sola versión (`IVAR_GROUP` … `IVAR_GROUP_END`, la ven en el ejercicio 5). Otro hilo ve la racha entera o nada.

**Para discutir.** En Smalltalk-80 con `Process` el mismo `n := n + 1` pierde incrementos si el scheduler cambia de proceso entre la lectura y la escritura; un store de un solo slot tampoco se rompe allí. Lo distinto acá es que hay paralelismo real y que la unidad atómica es la instantánea del objeto entero, no el slot. ¿Alcanza con eso, o el lenguaje debería ofrecer read-modify-write atómico sobre variables de instancia? Desde 0.17.0 existen `update:with:` sobre variables declaradas `atomic` y `compareAndSet:from:to:`. ¿Es la forma que esperaban, o preferirían algo más cerca de `Atom`?

### Ejercicio 3 — Cambiar la clase con instancias vivas (10 min, para casa)

`protost 03-forma-de-clase.st`

**Pregunta.** Completen la tabla del encabezado del archivo antes de correrlo: qué ve una instancia vieja y qué ve una nueva después de agregar métodos, de `addBehavior:` y de redeclarar la clase con otra variable de instancia.

**Discusión.** En protoCore la cadena de padres de un objeto se fija al construirlo.

- `>>` instala sobre el objeto clase existente, así que las instancias viejas ven el método nuevo.
- `addBehavior:` reconstruye la clase con un padre más y reasigna el nombre. Las instancias viejas se quedan con la clase anterior entera: no ven el mixin, no ven los métodos posteriores y ya no son `isKindOf:` de la clase que lleva el nombre (D21).
- Redeclarar una clase con la misma superclase reusa el objeto clase. Como las variables de instancia son atributos (`_iv_<nombre>`) y no slots indexados, no hay migración de forma: la variable nueva se lee `nil` en las instancias viejas. Desde 0.13.0 cada declaración de variables avanza una época de layout y la clase reconstruye su plantilla, así que las instancias nuevas nacen con la forma nueva.

**Para discutir.** Sin `become:` y sin slots indexados, ¿qué parte del problema clásico de cambio de forma desaparece y qué parte reaparece como "dos clases con el mismo nombre"?

## Laboratorio 1 (cont.) — El recolector

### Ejercicio 4 — Mirar al recolector mientras trabaja (15 min, para casa)

```bash
PROTOCORE_HEAP_TRACE=1 protost 04-gc-en-vivo.st 2> traza.txt
grep -c 'protoCore heap' traza.txt                                # ciclos
grep -o 'stw=[0-9.]*ms' traza.txt | sort -t '=' -k2 -n | tail -3  # pausas más largas
```

**Pregunta.** Un hilo asigna 500 000 `Array new: 8` y retiene uno de cada 250; después cuatro actores hacen lo mismo en paralelo. ¿La pausa crece con el heap? ¿Cuatro actores tardan lo mismo que uno?

**Qué leer en la traza.** Una línea por ciclo. `L` son las celdas vivas al terminar (de 64 bytes), `S` el límite blando del controlador adaptativo, `H` el presupuesto (75 % de la memoria física por omisión), los tres en celdas; `C` es la duración del ciclo concurrente y `stw` la pausa stop-the-world.

**Discusión.** Medido el 23/11, en tres corridas: 16 a 20 ciclos, ciclos concurrentes de hasta 84 ms y pausas de 0,02 a 0,13 ms, salvo una de 2,7 ms en una corrida. La pausa no depende del heap:

- el stop-the-world solo toma raíces precisas (los arreglos de slots de cada hilo y una cabeza de cadena joven por contexto) y copia las 256 raíces de la tabla de mutables, unos 2 KB;
- marcado, barrido y desmarcado corren concurrentes con los mutadores, **sin write barrier**: todo lo mutable vive en esas 256 raíces capturadas en la pausa (un SATB por instantánea, no por barrera);
- lo asignado después de la pausa es joven y no es candidato.

La pausa de 2,7 ms es una sola en tres corridas, con la máquina cargada; no sé de dónde sale y no la investigué. Es un buen ejemplo de por qué hace falta publicar percentiles (p99, p999) y no máximos de una corrida.

Cuatro actores hacen cuatro veces el trabajo de un hilo en 566 a 852 ms, contra 413 a 438 ms del hilo solo: entre 2 y 3 veces el rendimiento, no 4. El marcado es de un solo hilo, el barrido domina el costo del recolector y las recargas de celdas libres pasan por un mutex global (`protoCore/docs/GarbageCollector.md`). Compárenlo con el ejercicio 10, que casi no asigna.

**Variantes.**

- `PROTOCORE_HEAP_LIMIT_CELLS=2000000` fija un techo (2 000 000 celdas son 128 MB): medido el 23/11, 25 ciclos y los cuatro actores tardaron 2,7 s.
- `PROTOCORE_ADAPTIVE_HEAP=0` vuelve al techo fijo anterior a 0.6.2.

**Para discutir.** No hay generación vieja: cada ciclo vuelve a marcar todo lo vivo. ¿Qué harían ustedes con un objeto que vive todo el programa, sabiendo que el GC no mueve celdas y que la inmutabilidad elimina la write barrier?

## Laboratorio 2 — La VM

La referencia de esta parte es `docs/VM.md`: el orden de lectura del código (parser, compilador, módulo de bytecode, engine, primitivas), la codificación, el frame y la tabla de opcodes. Ténganlo abierto. `src/runtime/Opcodes.h` define hoy 59 opcodes más el prefijo `EXTEND`; dos de ellos (`PUSH_LATE_VAR`, `STORE_LATE_VAR`) nunca se ejecutan: se reescriben al instalar el método.

### Ejercicio 5 — Leer el bytecode (15 min, para casa)

`protost --disasm 05-bytecode.st` (los listados de `docs/vm-examples/` muestran otros casos)

**Pregunta.** Para cada método de `Muestra`, antes de mirar: ¿qué variables van al diccionario de capturadas y cuáles quedan en el frame? ¿Qué bloques se inlinean? ¿Dónde aparecen `BIN_INT_ADD`, `BIN_INT_MUL`, `IVAR_GROUP` y `JUMP_IF_NOT_BOOL`? En `contadores`, ¿los tres bloques comparten `c`?

**Qué van a ver.**

- `sumaHasta:` usa `to:do:` inlineado. Un cuerpo inlineado no es un ámbito de captura: `s` y el contador viven en slots del frame y el bucle es `BIN_INT_LE`, `BIN_INT_ADD` y `JUMP_BACK`, sin envíos. Desde 0.10.0 una asignación cuyo valor se descarta tampoco emite `DUP`/`POP`.
- `sumaConBloque:` hace lo mismo con `(1 to: n) do:`, que no se inlinea. `s` pasa al diccionario de capturadas (`MAKE_CAPTURED`, `STORE_CAPTURED`): un objeto **mutable** de protoCore cuyos atributos son las variables, encadenado por prototipo con el del ámbito de afuera. La cadena de ámbitos es una cadena de prototipos.
- `x:y:` compila las tres asignaciones como un grupo de escritura: `IVAR_GROUP`, `IVAR_GROUP`, `IVAR_GROUP_END`. Se publican en una sola versión.
- `signo:` muestra `JUMP_IF_NOT_BOOL` antes de cada salto. Si el receptor no es un Boolean, el código salta a un camino `DEOPT_ENTER` … `DEOPT_EXIT` que arma los bloques de verdad y **envía** `ifTrue:ifFalse:`, como Pharo 9 (D53). Hasta 0.9.x había un `ASSERT_BOOL_OR_DNU` que respondía `doesNotUnderstand:`.
- En `contadores`, `i * 10` es `BIN_INT_MUL` desde 0.10.0. Cada activación del bloque exterior abre su propio diccionario (`MAKE_CAPTURED 1`, hijo del heredado). Los tres bloques tienen cada uno su `c` (13, 21, 31).

**Para discutir.** No hay inline caches en los sitios de envío: la única caché es la de protoCore, por hilo, que desde 3.6.1 se indexa por (forma, nombre) y guarda la posición del atributo en la forma; una escritura de valores no la invalida (CHANGELOG de protoCore 3.6.1). ¿Dónde pondrían ustedes un inline cache en esta VM, y qué lo invalidaría?

### Ejercicio 6 — Retorno no local sin contextos (10 min, para casa)

`protost 06-retorno-no-local.st`

**Pregunta.** Cuatro casos de `^` dentro de un bloque: corrido por la primitiva `do:`, con el home ya retornado, con un `ensure:` en el camino y mandado a otro actor mientras el home sigue vivo.

**Discusión.** Los frames no son objetos: son registros de enteros sobre el arreglo de slots del hilo, y el engine no es recursivo. Un `^` cuyo home está en el mismo engine desapila frames; si el bloque lo corre una primitiva (`do:`), el home está en un engine exterior y el retorno viaja como excepción de C++, corriendo los `ensure:` al desenrollar. Si el home ya retornó, es un `BlockCannotReturn` capturable.

El caso 4 es el interesante. Saber si el home sigue vivo se resuelve con un registro thread-local de engines vivos. Desde otro hilo el home parece muerto, así que da `BlockCannotReturn` aunque el método esté esperando la respuesta. El texto del error nombra los dos casos desde 0.8.0 (S35). El ejercicio lo imprime en una línea `~`.

**Para discutir.** ¿Debería un `^` cruzar actores? Si sí, ¿qué significa para la garantía de un mensaje por vez del actor que corre el bloque? `thisContext` está reservado (D17) justamente porque no hay contexto que reificar; ¿qué mínimo de reificación haría falta para un depurador al estilo Smalltalk?

### Ejercicio 7 — Excepciones: buscar sin desenrollar (10 min)

`protost 07-excepciones.st`

**Pregunta.** Anoten el orden exacto de las líneas: `resume:`, `retry`, `ensure:` con `ifCurtailed:`, `ZeroDivide` reanudable y `outer`.

**Discusión.** `on:do:` es una primitiva que apila una entrada en una pila de handlers thread-local; no hay opcodes de excepciones. `signal` busca el handler y lo corre **encima** de los frames que señalaron, como en Pharo. Por eso el handler de `ZeroDivide` imprime antes que `ifCurtailed:` y `ensure:`: el desenrollado empieza cuando el handler termina. `resume:` devuelve el valor desde `signal`; `return:`, `retry` y caer al final del handler desenrollan con excepciones de C++. El informe del 20/10 midió las excepciones en unas 17 veces lo que cuestan en CPython, con 0.7.0; no lo volví a medir.

`outer` es el de Pharo desde 0.10.0 (D7, cerrado): el handler externo corre encima del interno; si reanuda, `outer` devuelve ese valor **dentro** del handler interno, que continúa. Por eso la salida termina en `'outer devolvió 5'` y `'interno continuó'`. Hasta 0.9.x se comportaba como `pass` y el resultado era `'cuerpo'`: es la línea que difiere si corren la copia del paquete (ver "Preparación"). `pass`, `retryUsing:` e `isResumable` también siguen a Pharo desde 0.10.0 (D54, D55).

**Para discutir.** Un depurador DAP que se detiene en el `signal` **antes** de que corra cualquier handler es posible justamente porque la búsqueda no desenrolla (`docs/debugging.md`). `outer` se resolvió sin contextos reificados, sobre la pila de handlers por hilo, y nunca cruza un mensaje de actor. ¿Qué otra parte del protocolo de excepciones de Pharo necesitaría contextos de verdad?

## Laboratorio 2 (cont.) — Actores, módulos e interoperación

### Ejercicio 8 — La semántica de los actores, caso por caso (15 min)

`protost 08-actores.st`

**Pregunta.** Seis casos: qué contesta el proxy sin encolar, dos actores que se esperan mutuamente, el mismo intercambio con `thenDo:`, un actor que devuelve el `Future` de otro, un error dentro del actor y el estado que queda después.

**Discusión.**

1. `asActor` devuelve un proxy sobre el objeto, que no cambia. Identidad, igualdad, `hash`, los tests de `nil`, `isActor` y la impresión se contestan en el acto; todo lo demás, `class` incluido, encola un sobre inmutable `{selector, args, future}` en un buzón MPSC y devuelve un `Future`. Los argumentos viajan por referencia, sin copia.
2. Un actor que hace `wait` no atiende otros mensajes (D37); es el precio de que su estado sean variables de instancia sin locks. Antes de estacionarse, `wait` sigue la cadena "este future lo resuelve tal actor, que espera a tal otro" y, si vuelve a sí mismo, señala `deadlock:` en lugar de colgarse.
3. `thenDo:` no estaciona a nadie. Ojo: el callback corre en el hilo que resuelve el future, en general el worker del otro actor, concurrente con quien lo registró. Desde 0.10.0 corre detrás de una barrera: no ve los handlers del código que resolvió el future (S36).
4. Un `Future` devuelto desde un actor **no se aplana**: `wait` da otro `Future`. JavaScript aplana las promesas; protoST no. ¿Debería?
5. El error vuelve con su clase a quien espera.
6. No hay rollback: el actor sigue vivo y la resta hecha antes del error queda (saldo 30).

**Esperar sin bloquear, y esperar ayudando.** Un `wait` en el cuerpo de un método de actor **no bloquea el hilo**: el engine guarda sus frames como objetos y libera el worker; al resolverse el future, el actor sigue en cualquier worker. Si el `wait` está dentro de un bloque que corre una primitiva (`do:`, `ensure:`, `on:do:`), los frames de C++ no se pueden guardar y el worker **ayuda**: corre mensajes de otros actores encima de la espera (`docs/LANGUAGE.md` §10.4). Ver `benchmarks/actors/cooperative_yield.st` con `PROTOST_WORKERS=2`: mil actores esperando sobre dos hilos.

**La lección del actor enterrado (0.16.1, S48).** La encontró protoBaseST, no un test de protoST. Su transporte TCP de cluster leía cada conexión en un turno de actor que duraba lo que la conexión. Un commit espera dentro de `ensure:`, así que su worker ayudaba; si tomaba el turno de un lector, el actor que había hecho el commit quedaba **enterrado** debajo: su `Future` ya estaba resuelto, pero no volvía hasta que se cerraba la conexión. El primer arreglo de protoBaseST (turnos finitos que el actor se re-envía) no alcanzó en 0.16.0: la ayuda corría el buzón entero del actor, incluidos los mensajes que se mandaba a sí mismo durante el turno, así que el turno no terminaba nunca; con `PROTOST_WORKERS=1` se detenía el programa entero, timers incluidos. 0.16.1 hace que una espera que ayuda corra **un mensaje por vez** y revise su `Future` después de cada uno (`STRuntime::helpOne`), y protoIO 0.7.1 acota por el timeout una escritura grande a un socket (S49). Lo que queda, a propósito y documentado: un worker que **no** espera sigue corriendo el buzón entero en un turno, auto-envíos incluidos (D59). Acotar esos turnos no compraría equidad con la cola de listos actual, que es LIFO; la equidad entre actores necesitaría una cola FIFO, y eso no está medido. Fuentes: `docs/STATUS.md` S48 y D59, y en protoBaseST `docs/not-done.md` ("Fixed issues").

**Para discutir.** No hay `Process`, `Semaphore`, `Delay` ni `fork`, ni preempción, buzones acotados o supervisión (`docs/ROADMAP.md`, tracks 12 a 14). ¿Qué de eso necesitan antes de escribir un servidor real? ¿Qué les dice que el bug lo haya encontrado un programa y no la suite?

### Ejercicio 9 — Módulos sin imagen (10 min, para casa)

`protost 09-modulos.st` (desde la carpeta de ejercicios)

**Pregunta.** ¿Cuántas veces se carga `geometria.st`? ¿Los dos `Import` dan el mismo objeto? ¿Se ve `_Ayudante` desde afuera? ¿De qué clase es el área?

**Discusión.** Un archivo `.st` es un módulo: `Import from:` lo busca en el directorio actual, en `$STPATH`, en el venv activo y en `lib/`; corre sus formas de primer nivel una sola vez, lo guarda en caché y exporta sus clases salvo las que empiezan con `_`. Debajo está UMD, el *Unified Module Discovery* de protoCore: cada espacio tiene una cadena de resolución de rutas y proveedores (`provider:st` es protoST), una caché de módulos por (proveedor, ruta, versión) y una tabla de raíces global para el GC. Los módulos no se descargan nunca. El área es una `Fraction` exacta (1420/113) porque `355 / 113` lo es.

**Interoperación: lectura guiada (sin correr).** Con los binarios instalados, `protost` todavía no carga módulos de otros lenguajes (con 0.16.1, `Import from: 'x.scala'` da `module not found: x.scala`): lo que funciona, funciona dentro de hosts de prueba que ponen dos runtimes en el mismo `ProtoSpace`. Lean, en este orden:

1. `docs/INTEROP.md` §4.3 y `src/modules/STForeignSend.h`: otro runtime manda un mensaje Smalltalk con `__st_send__`, un método de protoCore instalado en `Object` (receptor, selector y argumentos; corre como un envío real, con `doesNotUnderstand:` y primitivas).
2. En el repositorio de protoScala, `tests/conformance/tutorial/15-modules-calling-smalltalk.scala` y su `_smalltalk/bank.st`: Scala crea una cuenta Smalltalk, le manda `transfer:to:` como `transfer_to`, le pasa una clausura Scala que Smalltalk llama con `apply(...)` y atrapa `InsufficientFunds` por su clase.
3. En protoScala, `tests/unit/protost_interop.cpp`, el test que prueba que no hay copia: el mismo objeto tiene la misma dirección y el mismo hash en los dos lados.
4. `tests/umd/geometry_errors.st`: un módulo C++ de protoCore llamado con call-form (`Geo circleArea(-1)`) y su error atrapado con `on: Geo NegativeRadius do:`. Corre con `protost_umd_host`, que se compila desde las fuentes en Linux.

**Para discutir.** Dos lenguajes ven el mismo objeto sin copia porque comparten el núcleo, no un protocolo de serialización. ¿Qué pierde Smalltalk al convivir así (selectores contra nombres de método, `doesNotUnderstand:` contra excepciones del otro lado), y qué ganan los otros lenguajes?

### Ejercicio 10 — ¿Cuánto escala? (10 min)

```bash
PROTOST_WORKERS=1 protost 10-escalar.st
PROTOST_WORKERS=2 protost 10-escalar.st
PROTOST_WORKERS=4 protost 10-escalar.st
protost 10-escalar.st        # por omisión: un worker por hilo de hardware, hasta 8
```

**Pregunta.** Contar los primos hasta 120 000 en un hilo y repartido en 12 actores. ¿La aceleración es lineal hasta los núcleos físicos?

**Discusión.** Medido el 23/11 (dos corridas por punto, máquina cargada): 1,0× con un worker; 1,9 a 2,1× con dos; 2,4 a 3,4× con cuatro; 2,6 a 4,4× con seis; 3,7 a 3,8× con el pool por omisión (8 workers en esa máquina). La dispersión es la carga de la máquina, y por eso no es una curva. No hay lock global: el límite viene del costo de las escrituras (cada asignación a una variable capturada o de instancia publica una instantánea), del recolector cuando se asigna (ejercicio 4) y del pool, que por omisión no pasa de 8 workers. Los workers solo crecen, hasta 256, mientras hay hilos bloqueados en E/S o en una espera que llegó al tope de ayuda (S40).

**Para discutir.** Una VM de Smalltalk con green threads no escala nada en núcleos; una con un lock global, tampoco. ¿Qué aceleración tendría que mostrar protoST para que les importe, y en qué tipo de programa?

## Dirección: estado explícito (parte I en 0.17.0; clases valor, propuesta para 0.18.0)

**Qué salió y qué no.** La parte I del diseño está en la release [v0.17.0](https://github.com/gamarino/protoST/releases/tag/v0.17.0) (`docs/LANGUAGE.md` §4.13, `docs/STATUS.md` D60 a D63): los modificadores de variables de instancia, `update:with:` y `compareAndSet:from:to:`. Las **clases valor siguen siendo una propuesta para 0.18.0**: no están implementadas. La spec completa es `docs/superpowers/specs/2026-11-23-explicit-state-design.md` ([PR #25](https://github.com/gamarino/protoST/pull/25); el orden de entrega está en su sección 15).

**El problema.** El programador no podía decir qué estado está pensado para cambiar, cuál no debe cambiar nunca y cuál se comparte entre hilos y solo debe cambiar por compare-and-swap. protoBaseST lo tuvo que decir igual, por convención: un test que busca `:=` y los CAS a mano en las fuentes contra una lista de 59 excepciones permitidas, e inicializadores privados `_publish…:` escritos a mano. protoBaseST 0.4.x sigue corriendo sobre 0.17.0 (con el aviso de deprecación); la 0.5.0 migra.

**Lo que ya está en 0.17.0.**

- **Tres modificadores** de variables de instancia en clases comunes, declarados con `constantNames:`, `writeOnceNames:` y `atomicNames:`, y comprobados en **cada camino de escritura** (compilador, intérprete, reflexión, copia):
  - `constant`: solo se asigna en `initialize` (fuera de bloques) o en el constructor generado, y a lo sumo una vez; leerlo sin haberlo asignado señala `UnsetConstantError`.
  - `writeOnce`: un CAS desde nil; el que pierde la carrera es silencioso si su valor es `=` al instalado, y señala `ModificationForbidden` si no.
  - `atomic`: nunca con `:=` (error de compilación); solo con `update:with:`, que reintenta un bloque puro, o con `compareAndSet:from:to:`. Ambos los entiende cualquier objeto.
- **Constructor por keywords generado** sobre las constantes (`Account number: 10 owner: 'di'`), y la comprobación de fin de construcción: si `initialize` termina con una constante sin asignar, la construcción falla.
- **Un mismo patrón optimista** desde el atributo hasta el commit de protoBaseST: leer una instantánea, calcular sin efectos, publicar solo si sigue vigente, y si no, recalcular.
- **Deprecación:** `setInstVar:from:to:` pasa a ser un alias de `compareAndSet:from:to:` (con comprobación del nombre) que imprime un aviso una sola vez por stderr; se elimina en 0.18.0 (D63).

**Lo que sigue siendo propuesta (0.18.0).**

- **Clases valor** (`valueSubclass:`), cuyas instancias son objetos inmutables de protoCore, **nacidas completas** desde un constructor por keywords generado (`Point x: 1 y: 2`). `initialize` corre **antes** del nacimiento: valida y deriva sobre los valores recibidos, sin `self` (todavía no existe). No hay fase mutable seguida de un *freeze*, ni bloques constructores. `p withX: 3` da un valor nuevo y vuelve a correr `initialize`.
- **Invariantes sobre varios campos:** un valor inmutable dentro de una sola variable atómica, con `update:with:` sobre `withX:`. Sin STM, sin locks y sin cambios en protoCore. La mitad atómica ya funciona en 0.17.0; lo que falta es el valor inmutable que la complete.

**Hacia dónde apunta (dirección, no promesa).** La tesis de David P. Reed (MIT, 1978, *Naming and Synchronization in a Decentralized Computer System*) modela un objeto como una secuencia de versiones inmutables en pseudo-tiempo; de ahí desciende TeaTime, el protocolo de replicación de Croquet. En protoCore las versiones y las instantáneas salen casi gratis. Lo que sigue siendo trabajo real: ejecución determinista entre réplicas (los `Float` ya difieren entre plataformas en un caso, D58, y el planificador de actores no es determinista, D59), un *reflector* que ordene los mensajes externos, tiempo de mensaje en lugar de tiempo de reloj, y la identidad de los objetos entre réplicas.

**Para discutir.** Sobre lo que ya salió: ¿los nombres `constantNames:`, `writeOnceNames:`, `atomicNames:`, `update:with:` y `compareAndSet:from:to:` son los que escribirían (Q9)? Un `writeOnce` perdido con un valor distinto señala `ModificationForbidden` (Q5 se decidió así): ¿encaja con lo que escribirían? Sobre lo que falta, que todavía se puede cambiar: ¿encadenar automáticamente los `initialize` de una jerarquía de valores, o la convención de `super initialize` (Q1)? ¿Inmutabilidad superficial o profunda (Q3)? ¿Va un "mundo replicado" al menú de voluntarios (Q11)?

## Dónde meterse

Ninguna de estas tiene respuesta cerrada; elijan la que les interese. Un issue o un pull request con su test es la mejor forma de discutirla; `docs/VM.md` §4 y §5 explican cómo agregar un opcode y una primitiva, y cada test de conformidad es un `.st` con una directiva `"EXPECT: …"` en la primera línea. La sesión de protoBaseST tiene su propio [menú de proyectos voluntarios](https://github.com/gamarino/protoBaseST/blob/main/docs/talks/2026-12-03-fas-workshop/proyectos-voluntarios.md).

| Línea abierta | Dónde mirar | La pregunta de fondo |
| --- | --- | --- |
| Clases valor (0.18.0) y el resto de la spec de estado explícito | [PR #25](https://github.com/gamarino/protoST/pull/25), §15 y §18 | ¿Son los nombres, las reglas y el orden correctos para un programador Smalltalk, ahora que la parte I se puede probar? |
| Equidad entre actores | `docs/STATUS.md` D59 (cola de listos LIFO) | ¿Una cola FIFO compra equidad sin perder lo que da el turno por buzón entero? Hay que medirlo |
| Contextos y depuración: un mínimo de `thisContext` | `ExecutionEngine.h` (`snapshotFrames`, que ya materializa frames para el yield de los actores) | ¿Qué necesita de verdad un depurador Smalltalk? |
| Un inline cache en `SEND_*` | `ExecutionEngine.cpp`, `benchmarks/compare_builds.py` | Qué lo invalida cuando la caché de protoCore ya es por forma |
| Una generación vieja sin write barrier | `protoCore/docs/GarbageCollector.md` | ¿Se puede no re-marcar lo longevo sin barrera? |
| Percentiles de pausa y espera del quórum | `PROTOCORE_HEAP_TRACE` | Pausa y espera no son lo mismo; ¿cuánto esperan los mutadores? ¿De dónde sale una pausa de 2,7 ms? |
| Que un `^` cruce actores, o no | `ExecutionEngine.cpp` (registro de engines vivos), `STRuntime.cpp` | Retorno no local frente a la garantía de un mensaje por vez |
| Aplanar o no un `Future` de un `Future` | `src/primitives/future_prims.cpp`, `lib/kernel/future.st` | Promesas frente a la semántica de Smalltalk |
| `become:` de identidades mutables | la tabla de mutables de protoCore | La identidad es el ref, no la instantánea: ¿alcanza? |
| Comparar con Cuis y VAST | `benchmarks/comparable/` | Nunca medido: el primer número sería de ustedes |
| Un programa real suyo en ocho núcleos | `Import from: 'http'`, capítulo 15 del tutorial | Lo que ningún benchmark muestra (el actor enterrado lo encontró un programa real) |

## Referencias

| Tema | Documento |
| --- | --- |
| Lo que cambia para un programador Smalltalk | `docs/tutorial/14-for-the-smalltalk-programmer.md` |
| La VM, opcodes y ejemplos con su bytecode | `docs/VM.md`, `docs/vm-examples/` |
| Estado vivo, desvíos y bugs con su test | `docs/STATUS.md`, `KNOWN_ISSUES.md` |
| Qué cambió en cada versión | `CHANGELOG.md` (0.10.0 a 0.17.0 para este taller) |
| Actores, futures y la espera que ayuda | `docs/LANGUAGE.md` §10 (§10.4), capítulo 10 del tutorial |
| Estado explícito: lo publicado y la propuesta | `docs/LANGUAGE.md` §4.13, [release v0.17.0](https://github.com/gamarino/protoST/releases/tag/v0.17.0), spec en [PR #25](https://github.com/gamarino/protoST/pull/25) |
| Interoperación | `docs/INTEROP.md` (§4.0 y §4.3 están al día; §0 es del 29/9) |
| El recolector de protoCore | `protoCore/docs/GarbageCollector.md`, `protoCore/docs/MemoryModel.md` |
| Formas y listas en protoCore | CHANGELOG de protoCore 3.6.0 (RRB) y 3.6.1 (`ProtoShape`) |
| Módulos UMD | `protoCore/docs/MODULE_DISCOVERY.md` |
| Depuración con VS Code | `docs/debugging.md` |
| La sesión de protoBaseST | <https://github.com/gamarino/protoBaseST/tree/main/docs/talks/2026-12-03-fas-workshop> |
