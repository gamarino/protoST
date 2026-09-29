# Checklist — día de la charla

## La noche anterior

- [ ] Instalar protoCore 2.6.1 y protoST 0.5.0 en la notebook de la charla. En
      Windows, dentro de WSL2 con Ubuntu 24.04:
      `sudo apt install ./protoCore-2.6.1-Linux.deb ./protost-0.5.0-Linux.deb`
      (`apt install ./…` instala también las dependencias que falten). El
      paquete exige `protocore (>= 2.6.1)`.
- [ ] `protost --version` → `protoST 0.5.0`.
- [ ] `python3 --version` responde (lo usa la demo 4).
- [ ] `cd docs/talks/2026-10-15-fas/demos && RUNS=20 ./check_all.sh` → las
      cuatro demos 20/20.
- [ ] Probar las grabaciones:
      `scriptreplay --timing=../recordings/01-familiar-code.timing ../recordings/01-familiar-code.typescript`
      (y 02, 03, 04).
- [ ] Exportar el PDF del deck desde el artifact y guardarlo en esta
      carpeta (todavía no está); tener copia offline de ese PDF y del
      repositorio.

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
