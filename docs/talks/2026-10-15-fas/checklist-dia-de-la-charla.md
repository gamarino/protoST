# Checklist — día de la charla

## La noche anterior

- [ ] Instalar en la notebook de la charla la release publicada, la misma
      que instalará el público: protoCore 2.14.5 y protoST 0.6.2 (el código
      que midió el informe de rendimiento más la corrección S24 y el controlador adaptativo del heap como política por defecto). En Windows, dentro de WSL2 con
      Ubuntu 24.04:
      ```
      wget https://github.com/numaes/protoCore/releases/download/v2.14.5/protoCore-2.14.5-Linux.deb
      wget https://github.com/gamarino/protoST/releases/download/v0.6.2/protost-0.6.2-Linux.deb
      sudo apt install ./protoCore-2.14.5-Linux.deb ./protost-0.6.2-Linux.deb
      ```
      (`apt install ./…` instala también las dependencias que falten, y
      actualiza un protoCore 2.6.2 que haya quedado de 0.5.0). protoST 0.6.2
      exige protoCore 2.11.0 o posterior; 2.14.5 es la recomendada.
- [ ] `protost --version` → `protoST 0.6.2`;
      `ldd $(command -v protost) | grep protoCore` → `/usr/lib/x86_64-linux-gnu/libprotoCore.so.3`.
- [ ] `python3 --version` responde (lo usa la demo 4).
- [ ] `cd docs/talks/2026-10-15-fas/demos && RUNS=20 ./check_all.sh` → las
      cuatro demos 20/20.
- [ ] Probar las grabaciones:
      `scriptreplay --timing=../recordings/01-familiar-code.timing ../recordings/01-familiar-code.typescript`
      (y 02, 03, 04).
- [ ] Tener copia offline del PDF del deck (`slides/protoST-FAST-2026-es.pdf`,
      regenerado el 11/10 con las cifras nuevas) y del repositorio.

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
