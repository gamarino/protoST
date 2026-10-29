# protoST por dentro — guía de trabajo

Fundación Argentina de Smalltalk · 12/11/2026 · Gustavo Marino

## Para qué es esta guía

Esto no es un curso. protoST es una apuesta (un Smalltalk sobre un núcleo donde todo valor es inmutable, con hilos nativos y sin lock global) y no sé hasta dónde puede llegar. Los ejercicios están pensados para que cada uno ponga a prueba una decisión de diseño con sus propias manos, le vea el costo y saque su conclusión. Lo que más me sirve de ustedes es el criterio de quien construyó VMs de Smalltalk: dónde esto les parece prometedor, dónde les parece un error, y qué investigarían. Para aprender el lenguaje está `docs/tutorial/`, en especial el capítulo 14, escrito para quien ya sabe Smalltalk-80.

Cada ejercicio tiene la misma forma: una **pregunta**, un **programa de partida** que corre tal cual, una **predicción** que les pedimos anotar antes de correrlo, y una **discusión** con lo que hay detrás en el código de protoST o de protoCore. Los programas están en [`ejercicios/`](ejercicios/), con la salida esperada al lado. Las slides están en [`deck.html`](deck.html) y en [`slides/`](slides/) en PDF.

| Hora | Bloque | Qué se hace |
| --- | --- | --- |
| 0:00 | Apertura | Una apuesta, y por qué discutirla con ustedes |
| 0:10 | Parte I — el modelo | Identidad, estado y valor; estructuras persistentes; GC; decisiones de diseño; módulos; preguntas abiertas |
| 0:55 | Laboratorio 1 | Ejercicios 1 a 4 (modelo de objetos e inmutabilidad, GC) |
| 1:15 | Corte | 10 minutos |
| 1:25 | Parte II — la VM | Bytecode y despacho; los problemas propios de Smalltalk; interoperación; actores; preguntas abiertas |
| 2:15 | Laboratorio 2 | Ejercicios 5 a 10 (VM, actores, módulos e interoperación) |
| 2:50 | Cierre | Lo que no sé, dónde meterse, preguntas |

Trabajen de a dos si pueden: uno predice, el otro corre. Lo que no termine en la sala queda como tarea; la última sección propone líneas para seguir.

## Preparación (antes del 12/11)

Instalen antes de venir: en la sala no hay tiempo para compilar. Necesitan protoST 0.7.0 o posterior sobre protoCore 3.0.0 o posterior.

**Ubuntu 24.04 o Windows con WSL2 (recomendado).** Son dos paquetes `.deb` de las releases de GitHub:

```bash
# Windows, una sola vez, en PowerShell:
#   wsl --install -d Ubuntu-24.04
wget https://github.com/numaes/protoCore/releases/download/v3.0.0/protoCore-3.0.0-Linux.deb
wget https://github.com/gamarino/protoST/releases/download/v0.7.0/protost-0.7.0-Linux.deb
sudo apt install ./protoCore-3.0.0-Linux.deb ./protost-0.7.0-Linux.deb
protost --version          # protoST 0.7.0
```

Si ya tenían protoST 0.6.x, el mismo `apt install` actualiza protoCore (0.7.0 no corre sobre 2.14.x).

**macOS y Windows nativo.** No hay paquete publicado: se compila desde las fuentes (C++20, CMake 3.20+, protoCore 3.x instalado, protoIO 0.2.3, OpenSSL; en macOS también readline). Los pasos están en `docs/INSTALLATION.md`, incluida la sección *Windows (MSVC)*. Cuenten con una hora la primera vez.

**El repositorio.** Clonen `https://github.com/gamarino/protoST`: los ejercicios, el código de la VM y los listados de bytecode que vamos a leer están ahí.

**Verificación en dos minutos.**

1. `protost -e "(1/3) + (2/3)"` responde `1`.
2. `protost -e "100 factorial printString size"` responde `158`.
3. `protost -i` abre el REPL; salen con Ctrl-D.
4. `protost --disasm docs/vm-examples/05-fib.st` imprime el bytecode y después corre el programa. `--disasm` entró después de 0.7.0: si su `protost` no lo reconoce (`unknown option`), lean los listados ya generados en `docs/vm-examples/*.txt`; son salida real del desensamblador y un test verifica que no se desactualicen.
5. En la carpeta de ejercicios, `./verificar.sh` corre todos los programas y compara con la salida esperada; ninguno tarda más de unos segundos.

**Opcional: el depurador.** La extensión de VS Code (`editor-integration/vscode/`) habla DAP con `protost --dap`: breakpoints, paso a paso, excepciones, y varios hilos detenidos a la vez cuando hay actores. Ver `docs/debugging.md`.

**Máquina.** Cualquier x86-64 con 4 núcleos o más sirve. Las mediciones de paralelismo dependen de la carga: cierren lo que puedan y, en una notebook, conéctenla a la corriente.

## Laboratorio 1 — Identidad, estado y valor

Los tres primeros ejercicios ponen a prueba la decisión central: en protoCore todo valor es inmutable, y un objeto "mutable" es una identidad cuyo estado actual es una instantánea inmutable. Escribir es publicar una instantánea nueva con un compare-and-swap. Anoten la predicción antes de correr y antes de leer la discusión.

### Ejercicio 1 — Copiar es gratis (10 min)

`protost 01-copiar-es-gratis.st`

**Pregunta.** ¿Cuánto cuesta `copy` sobre un `Array` de un millón de elementos, y cuánto cuesta después cada `at:put:`?

**Discusión.** El `copy` es O(1): la copia es otra identidad que apunta a la misma lista persistente. Cada `at:put:` copia el camino de un árbol AVL (O(log n) celdas nuevas de 64 bytes) y publica la raíz nueva; en una notebook ronda 1,4 µs. Lo que pagan es la otra cara: 64 bytes por elemento en reposo y una construcción masiva que genera unas n·log n celdas transitorias (`KNOWN_ISSUES.md`, K5).

El `String` no tiene identidad mutable detrás: es el valor mismo (una cuerda de protoCore), por eso `at:put:` responde `ModificationForbidden` (D34).

**Para discutir.** ¿Qué idioms de Smalltalk dependen de que `copy` sea caro y `at:put:` barato? ¿Qué se rompe en un `WriteStream` que crece de a un carácter?

### Ejercicio 2 — Ninguna escritura se rompe, pero un incremento se puede perder (15 min)

`protost 02-actualizaciones-perdidas.st`

**Pregunta.** Ocho actores incrementan un mismo contador 100 000 veces cada uno. ¿Cuánto da con un objeto común compartido, con el contador como actor y con un `Atom`?

**Discusión.** Con el objeto común se pierde la mayoría de los incrementos (en una corrida: 140 634 de 800 000), pero el objeto nunca queda en un estado roto. Cada escritura de variable de instancia es atómica: el CAS sobre la tabla de mutables reintenta hasta publicar (`protoCore/core/ProtoObject.cpp`, camino de escritura). Lo que no es atómico es `n := n + 1`, que son dos operaciones: leer y publicar. Gana la última escritura.

Las dos correcciones son las dos herramientas del modelo:

- el **actor** serializa la lógica: un mensaje por vez;
- el **Atom** es una celda con compare-and-swap; `swap:` reintenta el bloque, que debe ser puro.

Una tercera, menos visible: una racha de asignaciones a variables de instancia se publica como una sola versión (`IVAR_GROUP` … `IVAR_GROUP_END`, la ven en el ejercicio 5). Otro hilo ve la racha entera o nada.

**Para discutir.** En Smalltalk-80 con `Process` el mismo `n := n + 1` pierde incrementos si el scheduler cambia de proceso entre la lectura y la escritura; un store de un solo slot tampoco se rompe allí. Lo distinto acá es que hay paralelismo real y que la unidad atómica es la instantánea del objeto entero, no el slot. ¿Alcanza con eso, o el lenguaje debería ofrecer read-modify-write atómico sobre variables de instancia (hoy existe `setInstVar:from:to:`)?

### Ejercicio 3 — Cambiar la clase con instancias vivas (10 min)

`protost 03-forma-de-clase.st`

**Pregunta.** Completen la tabla del encabezado del archivo antes de correrlo: qué ve una instancia vieja y qué ve una nueva después de agregar métodos, de `addBehavior:` y de redeclarar la clase con otra variable de instancia.

**Discusión.** En protoCore la cadena de padres de un objeto se fija al construirlo.

- `>>` instala sobre el objeto clase existente, así que las instancias viejas ven el método nuevo.
- `addBehavior:` reconstruye la clase con un padre más y reasigna el nombre. Las instancias viejas se quedan con la clase anterior entera: no ven el mixin, no ven los métodos posteriores y ya no son `isKindOf:` de la clase que lleva el nombre (D21).
- Redeclarar una clase con la misma superclase reusa el objeto clase. Como las variables de instancia son atributos (`_iv_<nombre>`) y no slots indexados, no hay migración de forma: la variable nueva se lee `nil` en las instancias viejas.

**Para discutir.** Sin `become:` y sin slots indexados, ¿qué parte del problema clásico de cambio de forma desaparece y qué parte reaparece como "dos clases con el mismo nombre"?

**Una trampa que encontró este ejercicio.** En 0.7.0, si un método terminaba en `^ variable` y la línea después de la línea en blanco empezaba con `(`, el parser la tomaba como argumento de un envío call-form `variable(...)` y la metía en el método, sin error. Está corregido en main (S32): el `(` de un call-form tiene que ir pegado al nombre, como los dos puntos de un keyword.

## Laboratorio 1 (cont.) — El recolector

### Ejercicio 4 — Mirar al recolector mientras trabaja (15 min)

```bash
PROTOCORE_HEAP_TRACE=1 protost 04-gc-en-vivo.st 2> traza.txt
grep -c 'protoCore heap' traza.txt                             # ciclos
grep -o 'stw=[0-9.]*ms' traza.txt | sort -t= -k2 -n | tail -3  # pausas más largas
```

**Pregunta.** Un hilo asigna 500 000 `Array new: 8` y retiene uno de cada 250; después cuatro actores hacen lo mismo en paralelo. ¿La pausa crece con el heap? ¿Cuatro actores tardan un cuarto?

**Qué leer en la traza.** Una línea por ciclo. `L` son las celdas vivas al terminar (de 64 bytes), `S` el límite blando del controlador adaptativo, `H` el presupuesto (75 % de la memoria física por omisión), `C` la duración del ciclo concurrente y `stw` la pausa stop-the-world.

**Discusión.** En una corrida de referencia hubo 27 ciclos, ciclos concurrentes de hasta 313 ms y pausas de 0,03 a 0,18 ms. La pausa no depende del heap:

- el stop-the-world solo toma raíces precisas (los arreglos de slots de cada hilo y una cabeza de cadena joven por contexto) y copia las 256 raíces de la tabla de mutables, unos 2 KB;
- marcado, barrido y desmarcado corren concurrentes con los mutadores, **sin write barrier**: todo lo mutable vive en esas 256 raíces capturadas en la pausa (un SATB por instantánea, no por barrera);
- lo asignado después de la pausa es joven y no es candidato.

Cuatro actores asignando **no** tardan un cuarto: en Windows tardaron 2,2 veces lo que un hilo para hacer cuatro veces el trabajo (1,8× de rendimiento). El marcado es de un solo hilo, el barrido domina el costo del recolector y las recargas de celdas libres pasan por un mutex global (`protoCore/docs/GarbageCollector.md`). Compárenlo con el ejercicio 10, que casi no asigna.

**Variantes.**

- `PROTOCORE_HEAP_LIMIT_CELLS=2000000` fija un techo (2 000 000 celdas son 128 MB): ¿cuántos ciclos más hay?
- `PROTOCORE_ADAPTIVE_HEAP=0` vuelve al techo fijo anterior a 0.6.2.
- Con `/usr/bin/time -v` (Linux) miren el RSS máximo: el heap de protoCore nunca devuelve memoria al sistema.

**Para discutir.** No hay generación vieja: cada ciclo vuelve a marcar todo lo vivo. ¿Qué harían ustedes con un objeto que vive todo el programa, sabiendo que el GC no mueve celdas y que la inmutabilidad elimina la write barrier?

## Laboratorio 2 — La VM

La referencia de esta parte es `docs/VM.md`: el orden de lectura del código (parser, compilador, módulo de bytecode, engine, primitivas), la codificación, el frame y los 43 opcodes. Ténganlo abierto.

### Ejercicio 5 — Leer el bytecode (15 min)

`protost --disasm 05-bytecode.st` (o, sin `--disasm`, los listados de `docs/vm-examples/`)

**Pregunta.** Para cada método de `Muestra`, antes de mirar: ¿qué variables van al diccionario de capturadas y cuáles quedan en el frame? ¿Qué bloques se inlinean? ¿Dónde aparecen `BIN_INT_ADD`, `IVAR_GROUP` y `ASSERT_BOOL_OR_DNU`? En `contadores`, ¿los tres bloques comparten `c`?

**Qué van a ver.**

- `sumaHasta:` usa `to:do:` inlineado. Desde 0.7.0 un cuerpo inlineado no es un ámbito de captura: `s` y el contador viven en slots del frame y el bucle es `BIN_INT_LE`, `BIN_INT_ADD` y `JUMP_BACK`, sin envíos.
- `sumaConBloque:` hace lo mismo con `(1 to: n) do:`, que no se inlinea. `s` pasa al diccionario de capturadas (`MAKE_CAPTURED`, `STORE_CAPTURED`): un objeto **mutable** de protoCore cuyos atributos son las variables, encadenado por prototipo con el del ámbito de afuera. La cadena de ámbitos es una cadena de prototipos.
- `x:y:` compila las tres asignaciones como un grupo de escritura: `IVAR_GROUP`, `IVAR_GROUP`, `IVAR_GROUP_END`. Se publican en una sola versión.
- `signo:` muestra `ASSERT_BOOL_OR_DNU` antes de cada salto: un receptor que no es Boolean da `doesNotUnderstand: #ifTrue:ifFalse:` en lugar de tomarse como verdadero.
- En `contadores`, cada activación del bloque exterior abre su propio diccionario (`MAKE_CAPTURED 1`, hijo del heredado). Los tres bloques tienen cada uno su `c` (13, 21, 31).

**Para discutir.** `*` no tiene camino rápido (`SEND_BINARY`), `+` sí. No hay inline caches en los sitios de envío: la única caché es la de protoCore, por hilo, indexada por (instantánea, símbolo), que se invalida sola cuando un objeto publica una instantánea nueva. ¿Dónde pondrían ustedes un inline cache en esta VM, y qué lo invalidaría?

### Ejercicio 6 — Retorno no local sin contextos (10 min)

`protost 06-retorno-no-local.st`

**Pregunta.** Cuatro casos de `^` dentro de un bloque: corrido por la primitiva `do:`, con el home ya retornado, con un `ensure:` en el camino y mandado a otro actor mientras el home sigue vivo.

**Discusión.** Los frames no son objetos: son registros de enteros sobre el arreglo de slots del hilo, y el engine no es recursivo. Un `^` cuyo home está en el mismo engine desapila frames; si el bloque lo corre una primitiva (`do:`), el home está en un engine exterior y el retorno viaja como excepción de C++, corriendo los `ensure:` al desenrollar. Si el home ya retornó, es un `BlockCannotReturn` capturable.

El caso 4 es el interesante. Saber si el home sigue vivo se resuelve con un registro thread-local de engines vivos. Desde otro hilo el home parece muerto, así que da `BlockCannotReturn` aunque el método esté esperando la respuesta. En 0.7.0 el texto decía que el home ya había retornado, lo que era falso; en main nombra los dos casos (S35). Comparen qué dice el suyo: el ejercicio lo imprime en una línea `~`.

**Para discutir.** ¿Debería un `^` cruzar actores? Si sí, ¿qué significa para la garantía de un mensaje por vez del actor que corre el bloque? `thisContext` está reservado (D17) justamente porque no hay contexto que reificar; ¿qué mínimo de reificación haría falta para un depurador al estilo Smalltalk?

### Ejercicio 7 — Excepciones: buscar sin desenrollar (10 min)

`protost 07-excepciones.st`

**Pregunta.** Anoten el orden exacto de las líneas: `resume:`, `retry`, `ensure:` con `ifCurtailed:`, `ZeroDivide` reanudable y `outer`.

**Discusión.** `on:do:` es una primitiva que apila una entrada en una pila de handlers thread-local; no hay opcodes de excepciones. `signal` busca el handler y lo corre **encima** de los frames que señalaron, como en Pharo. Por eso el handler de `ZeroDivide` imprime antes que `ifCurtailed:` y `ensure:`: el desenrollado empieza cuando el handler termina. `resume:` devuelve el valor desde `signal`; `return:`, `retry` y caer al final del handler desenrollan con excepciones de C++. Ese transporte es la causa de que las excepciones cuesten unas 17 veces lo que en CPython (informe del 20/10).

`outer` se comporta como `pass` (D7): el handler interno no continúa después de la reanudación, y el resultado es `'cuerpo'` y no `'interno continuó'`. Es un faltante, no una decisión.

**Para discutir.** Un depurador DAP que se detiene en el `signal` **antes** de que corra cualquier handler es posible justamente porque la búsqueda no desenrolla (`docs/debugging.md`). ¿Qué haría falta para implementar `outer` sobre esta pila de handlers?

## Laboratorio 2 (cont.) — Actores, módulos e interoperación

### Ejercicio 8 — La semántica de los actores, caso por caso (15 min)

`protost 08-actores.st`

**Pregunta.** Seis casos: qué contesta el proxy sin encolar, dos actores que se esperan mutuamente, el mismo intercambio con `thenDo:`, un actor que devuelve el `Future` de otro, un error dentro del actor y el estado que queda después.

**Discusión.**

1. `asActor` devuelve un proxy sobre el objeto, que no cambia. Identidad, igualdad, `hash`, los tests de `nil`, `isActor` y la impresión se contestan en el acto; todo lo demás, `class` incluido, encola un sobre inmutable `{selector, args, future}` en un buzón MPSC y devuelve un `Future`. Los argumentos viajan por referencia, sin copia.
2. Un actor que hace `wait` no atiende otros mensajes (D37); es el precio de que su estado sean variables de instancia sin locks. Antes de estacionarse, `wait` sigue la cadena "este future lo resuelve tal actor, que espera a tal otro" y, si vuelve a sí mismo, señala `deadlock:` en lugar de colgarse.
3. `thenDo:` no estaciona a nadie. Ojo: el callback corre en el hilo que resuelve el future, en general el worker del otro actor, concurrente con quien lo registró.
4. Un `Future` devuelto desde un actor **no se aplana**: `wait` da otro `Future`. JavaScript aplana las promesas; protoST no. ¿Debería?
5. El error vuelve con su clase a quien espera.
6. No hay rollback: el actor sigue vivo y la resta hecha antes del error queda (saldo 30).

Un `wait` dentro de un método de actor **no bloquea el hilo**: el engine guarda sus frames como objetos y libera el worker (yield cooperativo); al resolverse el future, el actor sigue en cualquier worker. Si el `wait` está dentro de un bloque que corre una primitiva (`do:`, `ensure:`), los frames de C++ no se pueden guardar y el worker "ayuda": corre mensajes de otros actores mientras espera. Ver `benchmarks/actors/cooperative_yield.st` con `PROTOST_WORKERS=2`: mil actores esperando sobre dos hilos.

**Para discutir.** No hay `Process`, `Semaphore`, `Delay` ni `fork`, ni preempción, buzones acotados o supervisión (`docs/ROADMAP.md`, tracks 12 a 14). ¿Qué de eso necesitan antes de escribir un servidor real?

### Ejercicio 9 — Módulos sin imagen (10 min)

`protost 09-modulos.st` (desde la carpeta de ejercicios)

**Pregunta.** ¿Cuántas veces se carga `geometria.st`? ¿Los dos `Import` dan el mismo objeto? ¿Se ve `_Ayudante` desde afuera? ¿De qué clase es el área?

**Discusión.** Un archivo `.st` es un módulo: `Import from:` lo busca en el directorio actual, en `$STPATH`, en el venv activo y en `lib/`; corre sus formas de primer nivel una sola vez, lo guarda en caché y exporta sus clases salvo las que empiezan con `_`. Debajo está UMD, el *Unified Module Discovery* de protoCore: cada espacio tiene una cadena de resolución de rutas y proveedores (`provider:st` es protoST), una caché de módulos por (proveedor, ruta, versión) y una tabla de raíces global para el GC. Los módulos no se descargan nunca. El área es una `Fraction` exacta (1420/113) porque `355 / 113` lo es.

**Interoperación: lectura guiada (sin correr).** Con los binarios instalados, `protost` no carga módulos de otros lenguajes todavía: lo que funciona, funciona dentro de hosts de prueba que ponen dos runtimes en el mismo `ProtoSpace`. Lean, en este orden:

1. `docs/INTEROP.md` §4.3 y `src/modules/STForeignSend.h`: otro runtime manda un mensaje Smalltalk con `__st_send__`, un método de protoCore instalado en `Object` (receptor, selector y argumentos; corre como un envío real, con `doesNotUnderstand:` y primitivas).
2. En el repositorio de protoScala, `tests/conformance/tutorial/15-modules-calling-smalltalk.scala` y su `_smalltalk/bank.st`: Scala crea una cuenta Smalltalk, le manda `transfer:to:` como `transfer_to`, le pasa una clausura Scala que Smalltalk llama con `apply(...)`, y atrapa `InsufficientFunds` por su clase.
3. En protoScala, `tests/unit/protost_interop.cpp`, el test que prueba que no hay copia: el mismo objeto tiene la misma dirección y el mismo hash en los dos lados.
4. `tests/umd/geometry_errors.st`: un módulo C++ de protoCore llamado con call-form (`Geo circleArea(-1)`) y su error atrapado con `on: Geo NegativeRadius do:`. Corre con `protost_umd_host`, que se compila desde las fuentes en Linux.

**Para discutir.** Dos lenguajes ven el mismo objeto sin copia porque comparten el núcleo, no un protocolo de serialización. ¿Qué pierde Smalltalk al convivir así (selectores contra nombres de método, `doesNotUnderstand:` contra excepciones del otro lado), y qué ganan los otros lenguajes?

### Ejercicio 10 — ¿Cuánto escala? (10 min)

```bash
PROTOST_WORKERS=1 protost 10-escalar.st
PROTOST_WORKERS=2 protost 10-escalar.st
PROTOST_WORKERS=4 protost 10-escalar.st
protost 10-escalar.st        # por omisión: un worker por núcleo, hasta 8
```

**Pregunta.** Contar los primos hasta 120 000 en un hilo y repartido en 12 actores. ¿La aceleración es lineal hasta los núcleos físicos?

**Discusión.** En una máquina de referencia (Windows, 0.7.0) dio 0,9×, 1,9×, 2,9× y 4,2× con 1, 2, 4 y 8 workers. No hay lock global: el límite viene del costo de las escrituras (cada asignación a una variable capturada o de instancia publica una instantánea), del recolector cuando se asigna (ejercicio 4) y del pool, que por omisión no pasa de 8 workers. Los workers solo crecen, hasta 256, mientras hay hilos bloqueados en E/S.

**Para discutir.** Una VM de Smalltalk con green threads no escala nada en núcleos; una con un lock global, tampoco. ¿Qué aceleración tendría que mostrar protoST para que les importe, y en qué tipo de programa?

## Dónde meterse

Ninguna de estas tiene respuesta cerrada; elijan la que les interese. Un issue o un pull request con su test es la mejor forma de discutirla; `docs/VM.md` §4 y §5 explican cómo agregar un opcode y una primitiva, y cada test de conformidad es un `.st` con una directiva `"EXPECT: …"` en la primera línea.

| Línea abierta | Dónde mirar | La pregunta de fondo |
| --- | --- | --- |
| Contextos y depuración: un mínimo de `thisContext` | `ExecutionEngine.h` (`snapshotFrames`, que ya materializa frames para el yield de los actores) | ¿Qué necesita de verdad un depurador Smalltalk? |
| Implementar `outer` (hoy es `pass`, D7) | `src/primitives/exception_prims.cpp` | Re-entrar un handler después de una reanudación sin contextos reificados |
| Un inline cache en `SEND_*` | `ExecutionEngine.cpp`, `benchmarks/compare_builds.py` | Qué invalida una caché cuando cada escritura publica una instantánea nueva |
| Una generación vieja sin write barrier | `protoCore/docs/GarbageCollector.md` | ¿Se puede no re-marcar lo longevo sin barrera? |
| Percentiles de pausa y espera del quórum | `PROTOCORE_HEAP_TRACE` | Pausa y espera no son lo mismo; ¿cuánto esperan los mutadores? |
| Que un `^` cruce actores, o no | `ExecutionEngine.cpp` (registro de engines vivos), `STRuntime.cpp` | Retorno no local frente a la garantía de un mensaje por vez |
| Aplanar o no un `Future` de un `Future` | `src/primitives/future_prims.cpp`, `lib/kernel/future.st` | Promesas frente a la semántica de Smalltalk |
| `become:` de identidades mutables | la tabla de mutables de protoCore | La identidad es el ref, no la instantánea: ¿alcanza? |
| Comparar con Cuis y VAST | `benchmarks/comparable/` | Nunca medido: el primer número sería de ustedes |
| Un programa real suyo en ocho núcleos | `Import from: 'http'`, capítulo 15 del tutorial | Lo que ningún benchmark muestra |

Preparando este taller aparecieron cuatro bugs reales: un envío call-form que cruzaba la línea en blanco, `super` resuelto por nombre con clases homónimas, el rango de `SmallInteger` y el mensaje del `^` entre actores. Los cuatro están corregidos en main, cada uno con su test (`docs/STATUS.md`, S32 a S35). Así se trabaja: se mide, se escribe el test, se arregla.

## Referencias

| Tema | Documento |
| --- | --- |
| Lo que cambia para un programador Smalltalk | `docs/tutorial/14-for-the-smalltalk-programmer.md` |
| La VM, opcodes y ejemplos con su bytecode | `docs/VM.md`, `docs/vm-examples/` |
| Estado vivo, desvíos y bugs con su test | `docs/STATUS.md`, `KNOWN_ISSUES.md` |
| Actores y futures | `docs/LANGUAGE.md` §10, capítulo 10 del tutorial |
| Interoperación | `docs/INTEROP.md` (§4.0 y §4.3 están al día; §0 es del 29/9) |
| El recolector de protoCore | `protoCore/docs/GarbageCollector.md`, `protoCore/docs/MemoryModel.md` |
| Módulos UMD | `protoCore/docs/MODULE_DISCOVERY.md` |
| Depuración con VS Code | `docs/debugging.md` |
| Rendimiento medido | `benchmarks/reports/` (el más reciente: 2026-10-20) |
