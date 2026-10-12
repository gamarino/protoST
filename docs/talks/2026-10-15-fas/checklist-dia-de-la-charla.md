# Checklist — día de la charla

## La noche anterior

- [ ] Instalar en la notebook de la charla la release publicada, la misma
      que instalará el público: protoCore 2.14.6 y protoST 0.6.5 (0.6.2, que
      hizo del controlador adaptativo del heap la política por defecto, más
      la liberación correcta de los pins en el despacho y el error claro
      cuando falta el kernel de 0.6.3, las demos dentro del paquete, en
      `/usr/share/protoST/talks/`, y, en 0.6.5, protoCore 2.14.6, con la
      aritmética entera sin temporales). En Windows,
      dentro de WSL2 con Ubuntu 24.04:
      ```
      wget https://github.com/numaes/protoCore/releases/download/v2.14.6/protoCore-2.14.6-Linux.deb
      wget https://github.com/gamarino/protoST/releases/download/v0.6.5/protost-0.6.5-Linux.deb
      sudo apt install ./protoCore-2.14.6-Linux.deb ./protost-0.6.5-Linux.deb
      ```
      (`apt install ./…` instala también las dependencias que falten y
      actualiza un protoCore anterior, como el 2.6.2 de 0.5.0, o un protoST
      anterior). Hay dos pisos distintos: el paquete
      `.deb` de protoST 0.6.5 exige protoCore 2.14.6 o posterior (lo declara
      el paquete); compilado desde las fuentes, protoST 0.6.5 necesita
      protoCore 2.11.0 o posterior.
- [ ] `protost --version` → `protoST 0.6.5`;
      `ldd $(command -v protost) | grep protoCore` →
      `libprotoCore.so.3 => /usr/bin/../lib/x86_64-linux-gnu/libprotoCore.so.3 (0x…)`:
      una ruta que termina en `lib/x86_64-linux-gnu/libprotoCore.so.3`, no
      `/usr/local` ni un directorio de build.
- [ ] `python3 --version` responde (lo usa la demo 4).
- [ ] Las demos vienen en el paquete:
      `cd /usr/share/protoST/talks/2026-10-15-fas/demos && RUNS=20 ./check_all.sh`
      → las cuatro demos 20/20 (lo mismo desde el repositorio, en
      `docs/talks/2026-10-15-fas/demos`). `run_demo.sh` usa el `protost` del
      `PATH`; para probar otro binario, `PROTOST=/ruta/al/protost`.
- [ ] Probar las grabaciones (están en el repositorio, no en el paquete):
      `scriptreplay --timing=../recordings/01-familiar-code.timing ../recordings/01-familiar-code.typescript`
      (y 02, 03, 04).
- [ ] Tener copia offline del PDF del deck (`slides/protoST-FAST-2026-es.pdf`,
      regenerado el 12/10 con las cifras sobre protoCore 2.14.6; también en
      `/usr/share/protoST/talks/2026-10-15-fas/slides/`), de las notas del
      orador (`notas-del-orador.md`) y del repositorio.

## Una hora antes

- [ ] Cerrar todo lo que use CPU (navegador con pestañas pesadas, builds,
      indexadores). La demo 2 muestra tiempos medidos en el momento: con la
      máquina cargada la aceleración cae (1,6–3,2× medido el 12/10 con un
      indexador del editor ocupando la CPU, en vez de unas 3,8×).
- [ ] Terminal: fuente grande (≥ 20 pt), fondo claro u oscuro según el
      proyector, ventana a pantalla completa.
- [ ] `cd` a la carpeta de demos; tener `less 01-familiar-code.st` listo para
      mostrar el código antes de correrlo.
- [ ] Desactivar notificaciones.

## Durante

- [ ] Si una demo falla o tarda: no depurar en vivo. Decirlo y pasar a la
      grabación (`scriptreplay`).
- [ ] Decir las cifras que aparecen en pantalla o las del informe, nunca de
      memoria.
