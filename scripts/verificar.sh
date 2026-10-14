#!/bin/bash
# Verifica recuperación de llaves, agotamiento del espacio y entradas inválidas.
set -euo pipefail
cd "$(dirname "$0")/.."
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FRASE='es una prueba de'
casos=0
for llave in 0 1 42 127 255; do
    ./bin/cifrador cifrar textos/prueba.txt "$TMP/c.bin" "$llave" >/dev/null
    salida=$(./bin/secuencial "$TMP/c.bin" "$FRASE" 8)
    grep -q "Llave encontrada: $llave$" <<< "$salida"
    for programa in naive ciclico permutado dinamico hibrido; do
        for np in 1 2 4; do
            salida=$(OMP_NUM_THREADS=2 timeout 15 mpirun --bind-to none -np "$np" \
                ./bin/"$programa" "$TMP/c.bin" "$FRASE" 8)
            grep -q "Llave encontrada: $llave$" <<< "$salida"
            casos=$((casos + 1))
        done
    done
done
./bin/cifrador cifrar textos/prueba.txt "$TMP/c.bin" 255 >/dev/null
for programa in naive ciclico permutado dinamico hibrido; do
    salida=$(OMP_NUM_THREADS=2 timeout 15 mpirun --bind-to none -np 4 \
        ./bin/"$programa" "$TMP/c.bin" "$FRASE" 4)
    grep -q 'Llave no encontrada' <<< "$salida"
    casos=$((casos + 1))
done
# Más procesos que llaves y varios hallazgos con una frase de un carácter.
for programa in naive ciclico permutado dinamico hibrido; do
    salida=$(OMP_NUM_THREADS=2 timeout 15 mpirun --bind-to none -np 4 \
        ./bin/"$programa" "$TMP/c.bin" "$FRASE" 1)
    grep -q 'Llave no encontrada' <<< "$salida"
    salida=$(OMP_NUM_THREADS=2 timeout 15 mpirun --bind-to none -np 4 \
        ./bin/"$programa" "$TMP/c.bin" 'a' 8)
    grep -q 'Llave encontrada' <<< "$salida"
    casos=$((casos + 2))
done
for argumentos in 'no_existe.bin frase 8' "$TMP/c.bin frase 0"; do
    salida=$(timeout 15 mpirun -np 4 ./bin/naive $argumentos 2>&1)
    grep -q 'Error:' <<< "$salida"
    casos=$((casos + 1))
done
echo "Verificación completada: $casos casos MPI y 5 recuperaciones secuenciales."
