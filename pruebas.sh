#!/bin/bash
# Bitacora de pruebas: secuencial vs ciclico con llaves facil/media/dificil
# (escaladas al espacio 2^N). Uso: ./pruebas.sh [N=22] [P=4]
N=${1:-22}; P=${2:-4}; PHRASE="es una prueba de"
S=$(python3 -c "print(2**$N)")
declare -A K
K[facil]=$(python3 -c "print($S//2+1)")
K[media]=$(python3 -c "print($S//2+$S//8)")
K[dificil]=$(python3 -c "import math;print(math.ceil($S/7+$S/13))")
K[naive_peor]=$(python3 -c "print($S//4-1)")
K[naive_mejor]=$(python3 -c "print($S//4+1)")
echo "N=$N (S=$S) P=$P"
for name in facil media dificil naive_mejor naive_peor; do
  k=${K[$name]}
  ./des_tool enc texto.txt /tmp/c_$name.bin $k > /dev/null
  seq=$(./secuencial /tmp/c_$name.bin "$PHRASE" $N | tail -1)
  cic=$(mpirun -np $P ./ciclico /tmp/c_$name.bin "$PHRASE" $N 2>&1 | grep -E "ganador|Iteraciones por")
  echo "--- $name | llave=$k"; echo "SEQ: $seq"; echo "$cic"
done
