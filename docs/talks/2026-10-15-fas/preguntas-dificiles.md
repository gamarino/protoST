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
contra CPython con el mismo trabajo y el mismo resultado verificado
(`benchmarks/reports/…-release-0.4.0.md`): hay casos más rápidos y casos
bastante más lentos (despacho recursivo de métodos, excepciones). Lo que sí
mostramos es que el mismo código usa varios núcleos.

**¿Cómo es el GC?**
El de protoCore: concurrente, con un hilo propio, que difiere la recolección
hasta que es necesaria y hace pausas cortas de stop-the-world. protoST fija un
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
`thisContext`, la jerarquía completa de metaclases, `become:`, imagen y
browser (por diseño), interoperabilidad real con otros runtimes (hoy un
programa protoScala recibe un objeto protoST sin copia pero no puede usarlo:
`docs/INTEROP.md` §0, `KNOWN_ISSUES.md` K4). La lista completa, verificada, es
el capítulo 14 del tutorial y `docs/STATUS.md`.

**¿Es estable? ¿Qué bugs abiertos tiene?**
0.4.0 pasa 977 casos de prueba. Durante la preparación de esta charla una
auditoría adversarial de unos 400 programas encontró errores silenciosos y
cuelgues; todos se corrigieron con su test. El cuelgue intermitente S19 se
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
