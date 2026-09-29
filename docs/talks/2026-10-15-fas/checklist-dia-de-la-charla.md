# Checklist — día de la charla

## La noche anterior

- [ ] Instalar el `.deb` de 0.4.0 en la notebook de la charla:
      `sudo dpkg -i protost-0.4.0-Linux.deb` (protoCore ≥ 2.5.0 instalado).
- [ ] `protost --version` → `protoST 0.4.0`.
- [ ] `cd docs/talks/2026-10-15-fas/demos && RUNS=20 ./check_all.sh` → las
      tres demos 20/20.
- [ ] Probar las grabaciones:
      `scriptreplay --timing=../recordings/01-familiar-code.timing ../recordings/01-familiar-code.typescript`
      (y 02, 03).
- [ ] Copia offline del deck (PDF exportado en esta carpeta) y del
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
