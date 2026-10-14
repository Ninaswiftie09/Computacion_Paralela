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
# Bloques menores que el espacio para comprobar pedidos sucesivos y residuos.
for bloque in 1 7 64; do
    for np in 1 2 3 4; do
        salida=$(timeout 15 mpirun --bind-to none -np "$np" ./bin/dinamico \
            "$TMP/c.bin" "$FRASE" 8 "$bloque")
        grep -q 'Llave encontrada: 255$' <<< "$salida"
        casos=$((casos + 1))
    done
done
salida=$(timeout 15 mpirun -np 4 ./bin/dinamico "$TMP/c.bin" "$FRASE" 8 0 2>&1)
grep -q 'Error: bloque' <<< "$salida"
salida=$(timeout 15 mpirun -np 4 ./bin/permutado "$TMP/c.bin" "$FRASE" 8 -1 2>&1)
grep -q 'Error: la semilla' <<< "$salida"
salida=$(timeout 15 mpirun -np 4 ./bin/naive "$TMP/c.bin" '' 8 2>&1)
grep -q 'Error: la frase clave está vacía' <<< "$salida"
printf '123' > "$TMP/invalido.bin"
salida=$(timeout 15 mpirun -np 4 ./bin/naive "$TMP/invalido.bin" "$FRASE" 8 2>&1)
grep -q 'no es un archivo cifrado' <<< "$salida"
casos=$((casos + 4))
echo "Verificación completada: $casos casos MPI y 5 recuperaciones secuenciales."
