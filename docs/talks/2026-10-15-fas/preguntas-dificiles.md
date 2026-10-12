# Preguntas difíciles — respuestas honestas

Cada respuesta dice qué está verificado y dónde. Si la respuesta es "no
sabemos" o "no funciona todavía", se dice así.

**¿Por qué no Pharo?**
Pharo es el entorno de Smalltalk vivo y protoST no compite con él. protoST
explora otra cosa: correr código Smalltalk desde archivos, sobre un núcleo
(protoCore) con hilos nativos sin lock global y otros lenguajes al lado. Si lo
que necesitan es el entorno vivo, Pharo.

**¿Dónde está la imagen? ¿Y el IDE?**
No hay imagen, por diseño: el programa es el archivo. Hay REPL (`protost -i`),
debugger de línea de comandos (`protost -d`) y un adaptador DAP para VS Code
(`protost --dap`, `docs/debugging.md`). No hay browser de clases.

**¿Rendimiento frente a Pharo?**
No medimos contra Pharo y no vamos a dar una cifra que no medimos. Medimos
contra CPython 3.14 con el mismo algoritmo, el mismo N y el mismo resultado
verificado (`benchmarks/reports/2026-10-11-protocore-2.14.5.md`: benchmarks sintéticos, en una notebook con un AMD Ryzen
5 5500U de 6 núcleos, `main` con protoCore 2.14.5):

- arranque: 32 ms (CPython: 36 ms);
- tiempo de proceso completo: de 1,0× a 26× el de CPython, media geométrica
  3,5×; con N chicos el arranque pesa mucho en cada tiempo;
- descontando el arranque de cada runtime, el trabajo en sí tarda unas 5×
  (bucle de enteros), 30× (`fib`, despacho recursivo de métodos) y 80×
  (excepciones) lo que tarda CPython; la media geométrica da entre 16× y 27×
  según la corrida (la resta es sensible al ruido con N chicos);
- en paralelo, doce actores corren 2,5–2,8× más rápido con el pool que con un
  worker, y `saturation_big` llega a unas 2× con cuatro workers; con cinco o
  seis no mejora.

El trabajo de octubre en el recolector de protoCore (2.10 → 2.14.5) no cambió
estas cifras: protoST ejecuta las mismas instrucciones que 0.5.0 y sus
programas recolectan poco con un techo de 640 MB. Los programas de actores que
asignan mucho midieron 4–6 % más lentos con un worker en una máquina
cargada; con la máquina quieta y todo compilado con el mismo compilador la
diferencia no supera el ruido de la medición (2–4 %): protoCore 2.12+ gasta
algo más de CPU del recolector en otro núcleo, que se nota sólo cuando todos
los núcleos están ocupados (`benchmarks/reports/2026-10-11-actor-regression-bisection.md`).
Buscándolo apareció un bug real, corregido en 0.6.1: una recursión sin
bucles (el fib de libro) nunca dejaba recolectar su basura; fib 29 se quedaba
sin memoria. Desde 0.6.2 protoST usa el controlador adaptativo del heap de
protoCore y dimensiona bajo demanda el arreglo de frames de cada hilo (K6):
frente a 0.6.1, los programas de actores van 16–43 % más rápido con entre un
quinto y la mitad de la memoria, y la demo 2 muestra una mediana de 4,45× en
vez de 4,3× (`benchmarks/reports/2026-10-12-adaptive-heap-default.md`).

Es decir: en un hilo protoST es bastante más lento que CPython. Lo que
mostramos es que el mismo código usa varios núcleos, hoy con ese techo.

**¿Cómo es el GC?**
El de protoCore: concurrente, con un hilo propio. Desde 0.6.2 protoST deja
que el controlador adaptativo de protoCore dimensione el heap: un
presupuesto del 75 % de la memoria física (o del límite del proceso) y,
dentro, un límite blando que arranca en 128 MB y sólo crece mientras el
programa espera al recolector. Si la memoria viva no entra, termina con un
mensaje claro en lugar de tomar la máquina. `PROTOCORE_HEAP_LIMIT_CELLS` fija
un techo fijo y `PROTOCORE_ADAPTIVE_HEAP=0` vuelve al techo de 640 MB de
0.6.1 (`KNOWN_ISSUES.md`, K5). Hasta 0.6.1 no se usaba porque con él
`fib.st` tardaba tres veces más; la causa era de protoST (una recursión no
entregaba su basura), corregida en 0.6.1. El barrido, la fase más cara,
recorre varias cadenas a la vez y puede sumar hilos auxiliares (2.14). Las
pausas stop-the-world que medimos en protoST en los benchmarks no pasan de
0,27 ms; antes de 0.6.2 llegaban a 25–31 ms con doce hilos, porque cada
pausa recorría entero un arreglo de frames de 1 M de entradas por hilo
(`KNOWN_ISSUES.md`, K6, resuelto).

**¿Por qué prototipos?**
protoCore es un modelo de prototipos; las clases de protoST son objetos
prototipo con nombre. Para el programador Smalltalk se ven como clases
(`subclass:`, métodos de clase, variables de clase y de instancia de clase).
Las diferencias que eso produce están en el capítulo 14.

**¿Con qué se compara el ecosistema protoCore? ¿Cuáles son sus referencias?**
Sus referencias son conceptuales, no de rendimiento, porque protoCore no
optimiza la forma actual de trabajar: intenta cambiar la base. Es un puente
entre la programación funcional y los lenguajes mutables.

- **Rich Hickey y Clojure** son la referencia más cercana. Su modelo de
  identidad, estado y valor dice que una identidad apunta a una sucesión de
  valores inmutables. Clojure lo ofrece *dentro* de un lenguaje funcional, con
  `atom` y `ref` explícitos. protoCore lo pone *debajo* de lenguajes mutables:
  un objeto mutable de Python, JavaScript o Smalltalk es, por construcción,
  una referencia atómica a una instantánea inmutable, sin que el programador
  cambie su forma de escribir. El código sigue siendo imperativo; la semántica
  de fondo es la de valores.
- **Estructuras de datos persistentes** (Okasaki; los árboles de Bagwell):
  hacen barato que "modificar" cree una versión nueva que comparte casi todo
  con la anterior.
- **Self y los prototipos de Lieberman:** un modelo de objetos sin clases
  impuestas, que permite que lenguajes muy distintos compartan objetos.
- **Erlang/BEAM, como contraste:** llega a la concurrencia segura por la vía
  opuesta, sin compartir nada y copiando los mensajes. protoCore comparte todo
  sin copiar, porque lo compartido es inmutable.

Lo que resulta es código imperativo de siempre con concurrencia segura, hilos
nativos sin lock global y objetos que cruzan de un lenguaje a otro en el mismo
proceso, por construcción y no por disciplina.

**¿Y GraalVM? ¿Y la JVM?**
No son comparables en lo que importa. GraalVM es una plataforma de uso general
optimizada para los lenguajes existentes, construida por completo sobre el
modelo de objetos de la JVM. No propone otra forma de trabajar: optimiza al
extremo la actual, y lo hace muy bien. protoCore cambia la base. Si la pregunta
es velocidad, ganan ellos sin discusión: protoCore no tiene JIT y no compite en
rendimiento de un solo hilo (ver la pregunta de rendimiento).

**¿Quién lo usa?**
Nadie en producción. Es un experimento y un demostrador de protoCore; buscamos
gente que lo pruebe con programas reales.

**¿Qué falta?**
`thisContext`, `outer` (hoy se comporta como `pass`), la jerarquía completa
de metaclases, `become:`, imagen y browser (por diseño), `Process`,
`Semaphore`, `Delay` y `fork` (la concurrencia son actores), clases como
`IdentityDictionary`, `ByteArray` o `ScaledDecimal`, y que los runtimes
instalados se carguen entre sí (el núcleo ya lo permite desde protoCore 2.6:
embebidos en un mismo proceso, protoScala lee y escribe objetos protoST sin
copiarlos; `docs/INTEROP.md` §0). Lo que encontramos está en el capítulo 14
del tutorial y en `docs/STATUS.md`; no es una garantía de que no haya otras
diferencias.

**¿Puede hablar con el mundo exterior?**
Desde 0.5.0 sí: archivos y directorios (`'datos.csv' asFileReference`), la
entrada y salida estándar (`Stdio stdin nextLine`: un script es un filtro
Unix), argumentos y variables de entorno, código de salida, otros programas
(`OSProcess`), sockets TCP y UDP, TLS, y HTTP cliente (también https) y
servidor, con un actor por conexión. Una espera de entrada/salida dentro de
un actor no frena al GC y el pool de workers crece mientras dura, así que
muchos actores esperando respuestas no dejan sin hilos a los que las
producen. Faltan HTTP/2, WebSockets y un servidor TLS. La demo 4 lo muestra.
La capa del sistema operativo es una biblioteca común, protoIO, que protoScala
y protoClojure también usan: la misma entrada/salida existe, con los nombres
de cada lenguaje, en los tres runtimes.

**¿Corre en Windows?**
El paquete publicado (0.6.2) es para Linux; en Windows funciona con WSL2 y
Ubuntu 24.04, instalando los mismos paquetes `.deb`. Las fuentes
compilan en forma nativa con Visual Studio 2022 y la integración continua
corre la suite completa en Windows (salvo dos tests que dependen de
herramientas POSIX: Ctrl-C enviado con `kill` y un ejemplo con sintaxis de
`sh`; `docs/INSTALLATION.md`, "Windows (MSVC)"); todavía
no hay un paquete de Windows publicado.

**¿Es estable? ¿Qué bugs abiertos tiene?**
0.5.0 pasaba sus 1068 casos de `ctest`; 0.6.2 (con protoCore 2.14.5)
pasa 1100, en Linux, macOS y Windows: programas de
conformidad, tests unitarios, los ejemplos, tests de la línea de comandos y
los ejemplos de la documentación. Durante la preparación de esta charla una
auditoría adversarial de unos 1100 programas, en dos rondas, encontró resultados incorrectos
silenciosos y cuelgues; cada corrección de un resultado incorrecto, un crash o
un cuelgue tiene su test de regresión. El cuelgue intermitente S19 se
capturó con el proceso vivo y resultó ser un despertar perdido en
`std::counting_semaphore` de libstdc++ 13; se reemplazó. Lo abierto está en
`docs/STATUS.md`.

**¿Los actores pueden bloquearse entre sí?**
Un actor que hace `wait` dentro de un método no atiende otros mensajes hasta
recibir la respuesta. Si dos actores se esperan mutuamente nadie avanza:
protoST lo detecta y señala un `Error` ("deadlock") en lugar de colgarse. Dentro
de actores conviene encadenar con `thenDo:` o `whenAll:`.

**¿Licencia?**
MIT.

**¿Cómo contribuyo?**
Escribir programas como los escribirían ustedes y reportar lo que sorprenda
(issues en el repositorio). `docs/ROADMAP.md` lista el trabajo abierto.
