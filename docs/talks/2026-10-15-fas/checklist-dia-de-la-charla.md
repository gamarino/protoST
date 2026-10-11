# Checklist — día de la charla

## La noche anterior

- [ ] Instalar en la notebook de la charla una de dos combinaciones (las
      dos pasan las cuatro demos, verificado el 11/10):
      - la que midió el informe de rendimiento: protoCore 2.14.5 y protoST
        de `main` (`4876884`), paquetes locales de `build_pkg_oct11`, no
        publicados;
      - la release publicada, la misma que instalará el público: protoCore
        2.6.2 y protoST 0.5.0. En Windows, dentro de WSL2 con Ubuntu 24.04:
        `sudo apt install ./protoCore-2.6.2-Linux.deb ./protost-0.5.0-Linux.deb`
        (`apt install ./…` instala también las dependencias que falten). El
        paquete exige `protocore (>= 2.6.1)`.
      Las cifras de rendimiento valen para las dos dentro del ruido, salvo
      los actores que asignan mucho con un worker, 4–6 % más lentos en `main`
      (el informe corre ambas lado a lado).
- [ ] `protost --version` → `protoST 0.5.0`.
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
