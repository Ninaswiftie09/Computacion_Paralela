#!/bin/bash
# Pruebas de la guía, partes B.1 y B.2, sobre el espacio completo de 2^56.
# B.1: cifra y descifra un texto con una llave sencilla.
# B.2: mide el tiempo de romper el cifrado con las tres llaves pedidas,
#      todas con 4 procesos.
#
# Uso: scripts/pruebas_guia.sh
#
# La llave 2^54 - 1 cae al final del rango del proceso 0 con 4 procesos, así
# que no termina en tiempo razonable. Se corta con un límite de tiempo.
set -u
cd "$(dirname "$0")/.." || exit 1

FRASE="es una prueba de"
TEXTO=textos/prueba.txt
TMP=$(mktemp -d)
LIMITE_B=${PRUEBAS_LIMITE_B:-20}
trap 'rm -rf "$TMP"' EXIT

titulo() { printf '\n===== %s =====\n' "$1"; }

titulo "B.1  Cifrar y descifrar con la llave 42"
./bin/cifrador cifrar "$TEXTO" "$TMP/c42.bin" 42
./bin/cifrador descifrar "$TMP/c42.bin" 42
echo "Con una llave incorrecta (43) sale texto ilegible:"
./bin/cifrador descifrar "$TMP/c42.bin" 43

titulo "B.2  Romper el cifrado con 4 procesos"
# a. llave sencilla
./bin/cifrador cifrar "$TEXTO" "$TMP/a.bin" 123456 > /dev/null
echo "--- a. llave 123456"
mpirun -np 4 ./bin/naive "$TMP/a.bin" "$FRASE" | grep -E "Llave|Proceso|Iteraciones del|Tiempo"

# c. primera llave del segundo proceso: se encuentra de inmediato
./bin/cifrador cifrar "$TEXTO" "$TMP/c.bin" 18014398509481984 > /dev/null
echo "--- c. llave 18014398509481984 (2^54, primera del proceso 1)"
mpirun -np 4 ./bin/naive "$TMP/c.bin" "$FRASE" | grep -E "Llave|Proceso|Iteraciones del|Tiempo"

# b. última llave del primer proceso: recorre todo su rango, no termina
./bin/cifrador cifrar "$TEXTO" "$TMP/b.bin" 18014398509481983 > /dev/null
echo "--- b. llave 18014398509481983 (2^54 - 1, última del proceso 0)"
echo "Se corta a los $LIMITE_B s para no esperar indefinidamente."
if timeout -k 2 "$LIMITE_B" mpirun -np 4 ./bin/naive "$TMP/b.bin" "$FRASE" \
        | grep -E "Llave|Tiempo"; then
    :
else
    echo "No terminó en $LIMITE_B s. El proceso 0 debe recorrer sus 2^54 llaves,"
    echo "unos 6 mil millones de segundos a la velocidad medida."
fi
