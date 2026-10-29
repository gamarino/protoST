#!/usr/bin/env bash
# Corre cada ejercicio y compara su salida con esperado/NN.out.
# Las líneas que empiezan con "~" son mediciones (tiempos, conteos que
# dependen de la carrera entre hilos) y no se comparan.
# Uso: ./verificar.sh            (usa el protost del PATH)
#      PROTOST=/ruta/protost ./verificar.sh
set -u
cd "$(dirname "$0")"
PROTOST="${PROTOST:-protost}"
echo "$("$PROTOST" --version)"
fallas=0
for f in [0-9][0-9]-*.st; do
  n="${f%%-*}"
  got="$("$PROTOST" "$f" 2>&1 | grep -v '^~' | tr -d '\r')"
  if [ "$got" == "$(tr -d '\r' < "esperado/$n.out")" ]; then
    echo "ok     $f"
  else
    echo "DIFIERE $f"
    diff <(echo "$got") <(tr -d '\r' < "esperado/$n.out") | head -20
    fallas=$((fallas + 1))
  fi
done
[ "$fallas" -eq 0 ] && echo "todos los ejercicios dan lo esperado" || echo "$fallas ejercicio(s) difieren"
exit "$fallas"
