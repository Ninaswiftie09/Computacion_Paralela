#!/bin/bash
# Bitácora de tiempos. Escala el espacio a 2^N y mide cada programa con
# llaves de distinta dificultad y distinto número de procesos. Repite cada
# caso varias veces y escribe una fila por corrida en formato CSV.
#
# Uso: scripts/bitacora.sh [N] [repeticiones] > resultados/bitacora.csv
#   N             bits del espacio de llaves. Por omisión, 23.
#   repeticiones  corridas por caso. Por omisión, 5.
#
# Las llaves siguen las definiciones de la guía, escaladas a S = 2^N:
#   facil        S/2 + 1
#   media        S/2 + S/8
#   dificil      techo(S/7 + S/13)
#   naive_mejor  S/4 + 1   (primera del segundo proceso con 4 procesos)
#   naive_peor   S/4 - 1   (última del primer proceso con 4 procesos)
set -u
cd "$(dirname "$0")/.." || exit 1

N=${1:-23}
REPETICIONES=${2:-5}
FRASE="es una prueba de"
TEXTO=textos/prueba.txt
PROCESOS="1 2 4 8"
HIBRIDO_CONFIGS="1:2 1:4 1:8 2:4 4:2"
LIMITE=180
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

S=$(python3 -c "print(2**$N)")
declare -A LLAVE
LLAVE[facil]=$(python3 -c "print($S//2 + 1)")
LLAVE[media]=$(python3 -c "print($S//2 + $S//8)")
LLAVE[dificil]=$(python3 -c "import math; print(math.ceil($S/7 + $S/13))")
LLAVE[naive_mejor]=$(python3 -c "print($S//4 + 1)")
LLAVE[naive_peor]=$(python3 -c "print($S//4 - 1)")
DIFICULTADES="facil media dificil naive_mejor naive_peor"

# Extrae "Tiempo" y las iteraciones del ganador de la salida de un programa,
# y escribe una fila del CSV.
fila() {
    local programa=$1 dificultad=$2 llave=$3 procesos=$4 hilos=$5 repeticion=$6 salida=$7
    local segundos iteraciones
    segundos=$(sed -n 's/^Tiempo: \(.*\) s/\1/p' <<< "$salida")
    iteraciones=$(sed -n 's/^Iteraciones del proceso ganador: //p' <<< "$salida")
    [ -z "$iteraciones" ] && iteraciones=$(sed -n 's/^Iteraciones: //p' <<< "$salida")
    echo "$programa,$dificultad,$llave,$N,$procesos,$hilos,$repeticion,${iteraciones:-NA},${segundos:-NA}"
}

echo "programa,dificultad,llave,bits,procesos,hilos,repeticion,iter_ganador,segundos"

for dificultad in $DIFICULTADES; do
    k=${LLAVE[$dificultad]}
    cifrado="$TMP/$dificultad.bin"
    ./bin/cifrador cifrar "$TEXTO" "$cifrado" "$k" > /dev/null
    for rep in $(seq 1 "$REPETICIONES"); do
        salida=$(timeout -k 2 "$LIMITE" ./bin/secuencial "$cifrado" "$FRASE" "$N")
        fila secuencial "$dificultad" "$k" 1 1 "$rep" "$salida"

        for programa in naive ciclico; do
            for np in $PROCESOS; do
                salida=$(timeout -k 2 "$LIMITE" mpirun -np "$np" ./bin/"$programa" "$cifrado" "$FRASE" "$N")
                fila "$programa" "$dificultad" "$k" "$np" 1 "$rep" "$salida"
            done
        done

        # El permutado es determinista por semilla. Cada repetición usa una
        # semilla distinta para medir el tiempo esperado, no el de un orden fijo.
        for np in $PROCESOS; do
            salida=$(timeout -k 2 "$LIMITE" mpirun -np "$np" ./bin/permutado "$cifrado" "$FRASE" "$N" "$rep")
            fila permutado "$dificultad" "$k" "$np" 1 "$rep" "$salida"
        done

        for config in $HIBRIDO_CONFIGS; do
            np=${config%:*}; hilos=${config#*:}
            salida=$(timeout -k 2 "$LIMITE" env OMP_NUM_THREADS="$hilos" \
                     mpirun -np "$np" ./bin/hibrido "$cifrado" "$FRASE" "$N")
            fila hibrido "$dificultad" "$k" "$np" "$hilos" "$rep" "$salida"
        done
    done
done
