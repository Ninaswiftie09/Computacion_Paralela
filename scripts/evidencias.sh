#!/bin/bash
# Registra compilación, entorno, cifrado y pruebas de la guía.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p resultados
{
    echo '$ make -B'
    make -B
} > resultados/compilacion.txt 2>&1
{
    echo '$ uname -sr'
    uname -sr
    echo '$ lscpu (resumen)'
    lscpu | grep -E 'Model name|CPU\(s\)|Thread|Core|Socket'
    echo '$ gcc --version'
    gcc --version | head -n 1
    echo '$ mpirun --version'
    mpirun --version | head -n 1
    echo '$ openssl version'
    openssl version
    echo 'Ejecución en contenedor Debian 13 sobre Docker Desktop y WSL2.'
    echo 'MPI sin fijación de procesos a núcleos: --bind-to none.'
} > resultados/entorno.txt
{
    echo '$ bash scripts/verificar.sh'
    bash scripts/verificar.sh
} > resultados/verificacion.txt 2>&1
{
    echo '$ bash scripts/pruebas_guia.sh'
    bash scripts/pruebas_guia.sh
} > resultados/guia.txt 2>&1
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
{
    echo '$ ./bin/cifrador cifrar textos/prueba.txt temporal.bin 42'
    ./bin/cifrador cifrar textos/prueba.txt "$TMP/c42.bin" 42
    echo '$ openssl enc -des-ecb -K 5401010101010101 -nopad -provider legacy -provider default'
    openssl enc -des-ecb -K 5401010101010101 -nopad -provider legacy -provider default \
        -in textos/prueba.txt -out "$TMP/openssl.bin"
    cmp "$TMP/c42.bin" "$TMP/openssl.bin"
    echo 'Comparación byte a byte: archivos iguales.'
} > resultados/openssl.txt 2>&1
echo 'bits,llave,iteraciones,segundos' > resultados/secuencial_bits.csv
{
    for bits in 16 20 22 24; do
        llave=$(( (1 << bits) - 1 ))
        ./bin/cifrador cifrar textos/prueba.txt "$TMP/c.bin" "$llave" >/dev/null
        echo "\$ ./bin/secuencial temporal.bin 'es una prueba de' $bits (llave $llave)"
        salida=$(./bin/secuencial "$TMP/c.bin" 'es una prueba de' "$bits")
        printf '%s\n' "$salida"
        tiempo=$(sed -n 's/^Tiempo: \(.*\) s/\1/p' <<< "$salida")
        echo "$bits,$llave,$((llave + 1)),$tiempo" >> resultados/secuencial_bits.csv
    done
} > resultados/secuencial_bits.txt
echo 'Evidencias registradas en resultados/.'
