#!/bin/bash
# Bitácora de pruebas: secuencial contra cíclico con llaves fácil, media y
# difícil, escaladas al espacio 2^N.
# Uso: scripts/pruebas.sh [N=22] [P=4]
N=${1:-22}; P=${2:-4}; FRASE="es una prueba de"
S=$(python3 -c "print(2**$N)")
declare -A K
K[facil]=$(python3 -c "print($S//2+1)")
K[media]=$(python3 -c "print($S//2+$S//8)")
K[dificil]=$(python3 -c "import math;print(math.ceil($S/7+$S/13))")
K[naive_peor]=$(python3 -c "print($S//4-1)")
K[naive_mejor]=$(python3 -c "print($S//4+1)")
echo "N=$N (S=$S) P=$P"
for nombre in facil media dificil naive_mejor naive_peor; do
  k=${K[$nombre]}
  ./bin/cifrador cifrar textos/prueba.txt /tmp/c_$nombre.bin $k > /dev/null
  sec=$(./bin/secuencial /tmp/c_$nombre.bin "$FRASE" $N | grep -E "Iteraciones|Tiempo" | paste -sd' ')
  cic=$(mpirun -np $P ./bin/ciclico /tmp/c_$nombre.bin "$FRASE" $N 2>&1 | grep -E "ganador|Iteraciones por")
  echo "--- $nombre | llave=$k"; echo "SEC: $sec"; echo "$cic"
done
