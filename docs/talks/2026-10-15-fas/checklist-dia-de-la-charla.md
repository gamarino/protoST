# Checklist — día de la charla

## La noche anterior

- [ ] Instalar el `.deb` de 0.4.1 en la notebook de la charla:
      `sudo dpkg -i protost-0.4.1-Linux.deb`. Antes, instalar protoCore:
      el paquete depende de `protocore (>= 2.5.0)` y `protocore (<< 3.0.0)`
      (`dpkg-deb -I protost-0.4.1-Linux.deb`); en la máquina de desarrollo
      está instalado protoCore 2.5.0.
- [ ] `protost --version` → `protoST 0.4.1`.
- [ ] `cd docs/talks/2026-10-15-fas/demos && RUNS=20 ./check_all.sh` → las
      tres demos 20/20.
- [ ] Probar las grabaciones:
      `scriptreplay --timing=../recordings/01-familiar-code.timing ../recordings/01-familiar-code.typescript`
      (y 02, 03).
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
