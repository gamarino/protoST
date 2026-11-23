# protoST por dentro — FAST, 3/12/2026

Primera sesión del taller de la Fundación Argentina de Smalltalk (de 2 a 3
horas en total): el modelo de inmutabilidad de protoCore, su recolector, la
VM de protoST, los actores y la dirección del lenguaje (estado explícito, una
propuesta en revisión), planteados como preguntas abiertas para discutir con
quienes construyen VMs de Smalltalk. protoST se presenta como lo que es: un
demostrador de protoCore y una base para gemelos digitales, no un clon de
Smalltalk. La segunda sesión, de duración parecida, es protoBaseST, con su
propio deck y su menú de proyectos voluntarios:
<https://github.com/gamarino/protoBaseST/tree/main/docs/talks/2026-12-03-fas-workshop>.

Este material reemplaza al del taller previsto para el 12/11 (que estaba en
`docs/talks/2026-11-12-fas-workshop/`), puesto al día para protoST 0.16.1. La
charla anterior, del 15/10, está en `../2026-10-15-fas/`.

| Archivo | Qué es |
| --- | --- |
| `deck.html` | Las slides (fuente): una `<section>` por slide, notas del orador en `<aside>` |
| `slides/protoST-por-dentro-FAST-2026-12-03.pdf` | Las mismas slides en PDF |
| `render_pdf.py` | Regenera el PDF desde `deck.html` con Chrome, Chromium o Edge headless |
| `guia-de-trabajo.md` | La guía del taller: agenda de las dos sesiones, preparación, diez ejercicios, la propuesta de estado explícito y líneas abiertas |
| `ejercicios/` | Los programas, su salida esperada (`esperado/`) y `verificar.sh` |

`ejercicios/verificar.sh` corre los diez programas y compara su salida con la
esperada, salvo las líneas que empiezan con `~` (tiempos y conteos que
dependen de la máquina). Pasa con protoST 0.16.1 (verificado el 23/11/2026).
Con versiones anteriores a 0.10.0 el ejercicio 7 difiere en sus dos últimas
líneas, porque `outer` se comportaba como `pass` (STATUS D7).
