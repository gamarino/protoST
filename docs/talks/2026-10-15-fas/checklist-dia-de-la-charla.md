# Checklist — día de la charla

## La noche anterior

- [ ] Instalar en la notebook de la charla la release publicada, la misma
      que instalará el público: protoCore 2.14.5 y protoST 0.6.3 (0.6.2, que
      hizo del controlador adaptativo del heap la política por defecto, más
      la liberación correcta de los pins en el despacho, el error claro
      cuando falta el kernel y las demos dentro del paquete). En Windows,
      dentro de WSL2 con Ubuntu 24.04:
      ```
      wget https://github.com/numaes/protoCore/releases/download/v2.14.5/protoCore-2.14.5-Linux.deb
      wget https://github.com/gamarino/protoST/releases/download/v0.6.3/protost-0.6.3-Linux.deb
      sudo apt install ./protoCore-2.14.5-Linux.deb ./protost-0.6.3-Linux.deb
      ```
      (`apt install ./…` instala también las dependencias que falten y
      actualiza un protoCore 2.6.2 que haya quedado de 0.5.0, o un protoST
      anterior). Hay dos pisos distintos: el paquete `.deb` de protoST 0.6.3
      exige protoCore 2.14.5 o posterior (lo declara el paquete); compilado
      desde las fuentes, protoST 0.6.3 necesita protoCore 2.11.0 o posterior.
- [ ] `protost --version` → `protoST 0.6.3`;
      `ldd $(command -v protost) | grep protoCore` →
      `libprotoCore.so.3 => /usr/bin/../lib/x86_64-linux-gnu/libprotoCore.so.3 (0x…)`:
      una ruta que termina en `lib/x86_64-linux-gnu/libprotoCore.so.3`, no
      `/usr/local` ni un directorio de build.
- [ ] `python3 --version` responde (lo usa la demo 4).
- [ ] Las demos vienen en el paquete:
      `cd /usr/share/doc/protoST/talks/2026-10-15-fas/demos && RUNS=20 ./check_all.sh`
      → las cuatro demos 20/20 (lo mismo desde el repositorio, en
      `docs/talks/2026-10-15-fas/demos`). `run_demo.sh` usa el `protost` del
      `PATH`; para probar otro binario, `PROTOST=/ruta/al/protost`.
- [ ] Probar las grabaciones (están en el repositorio, no en el paquete):
      `scriptreplay --timing=../recordings/01-familiar-code.timing ../recordings/01-familiar-code.typescript`
      (y 02, 03, 04).
- [ ] Tener copia offline del PDF del deck (`slides/protoST-FAST-2026-es.pdf`,
      regenerado el 12/10 con las cifras de 0.6.2; también en
      `/usr/share/doc/protoST/talks/2026-10-15-fas/slides/`), de las notas del
      orador (`notas-del-orador.md`) y del repositorio.

## Una hora antes

- [ ] Cerrar todo lo que use CPU (navegador con pestañas pesadas, builds,
      indexadores). La demo 2 muestra tiempos medidos en el momento.
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
