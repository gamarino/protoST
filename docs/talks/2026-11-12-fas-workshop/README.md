# protoST por dentro — FAST, 12/11/2026

Charla y taller de tres horas para la Fundación Argentina de Smalltalk: el
modelo de inmutabilidad de protoCore, su recolector, la VM de protoST, la
interoperación y los actores, planteados como preguntas abiertas para
discutir con quienes construyen VMs de Smalltalk. Sigue a la charla del
15/10 (`../2026-10-15-fas/`).

| Archivo | Qué es |
| --- | --- |
| `deck.html` | Las slides (fuente): una `<section>` por slide, notas del orador en `<aside>` |
| `slides/protoST-por-dentro-FAST-2026-11-12.pdf` | Las mismas slides en PDF |
| `render_pdf.py` | Regenera el PDF desde `deck.html` con Chrome, Chromium o Edge headless |
| `guia-de-trabajo.md` | La guía del taller: preparación, diez ejercicios y líneas abiertas |
| `ejercicios/` | Los programas, su salida esperada (`esperado/`) y `verificar.sh` |

`ejercicios/verificar.sh` corre los diez programas y compara su salida con la
esperada, salvo las líneas que empiezan con `~` (tiempos y conteos que
dependen de la máquina). Pasa con protoST 0.7.0 y con main.
