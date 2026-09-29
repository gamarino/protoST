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
verificado (`benchmarks/reports/2026-09-29-release-0.4.0.md`, en una notebook
con un AMD Ryzen 5 5500U de 6 núcleos):

- arranque: 27 ms (CPython: 29 ms);
- tiempo de proceso completo: de 1,0× a 23,6× el de CPython, media
  geométrica 3,45×; con N chicos el arranque pesa mucho en cada tiempo;
- descontando el arranque de cada runtime, el trabajo en sí tarda unas 6×
  (bucle de enteros), 36× (`fib`, despacho recursivo de métodos) y 64×
  (excepciones) lo que tarda CPython; media geométrica 20,8×;
- en paralelo, el techo medido de esta versión es de unas 2,1× (cuatro
  workers contra uno; con cinco o seis no mejora), confirmado por una segunda
  corrida independiente con menos carga.

Es decir: en un hilo protoST es bastante más lento que CPython. Lo que
mostramos es que el mismo código usa varios núcleos, hoy con ese techo.

**¿Cómo es el GC?**
El de protoCore: concurrente, con un hilo propio, que difiere la recolección
hasta que es necesaria. Tiene fases de stop-the-world; no medimos cuánto
duran en protoST, así que no damos una cifra. protoST fija un
techo de heap (640 MB por defecto, configurable con PROTOCORE_HEAP_LIMIT_CELLS) y si se agota
termina con un mensaje claro en lugar de tomar la máquina.

**¿Por qué prototipos?**
protoCore es un modelo de prototipos; las clases de protoST son objetos
prototipo con nombre. Para el programador Smalltalk se ven como clases
(`subclass:`, métodos de clase, variables de clase y de instancia de clase).
Las diferencias que eso produce están en el capítulo 14.

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

**¿Corre en Windows?**
No en forma nativa: la entrada/salida es POSIX. En Windows funciona con WSL2
y Ubuntu 24.04, instalando los mismos paquetes `.deb`.

**¿Es estable? ¿Qué bugs abiertos tiene?**
0.5.0 pasa los 1068 casos de `ctest`: programas de
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
