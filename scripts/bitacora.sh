#!/bin/bash
# Bitácora de tiempos con una misma llave y distintas configuraciones.
#
# Uso: scripts/bitacora.sh [N] [repeticiones] [llave] > resultados/bitacora.csv
#   N             bits del espacio de llaves. Por omisión, 24.
#   repeticiones  corridas por caso. Por omisión, 5.
#
#   llave         valor usado para cifrar. Por omisión, 16700000.
set -euo pipefail
cd "$(dirname "$0")/.." || exit 1

N=${1:-24}
REPETICIONES=${2:-5}
LLAVE_EVALUACION=${3:-16700000}
if ! [[ "$N" =~ ^[0-9]+$ && "$REPETICIONES" =~ ^[0-9]+$ && "$LLAVE_EVALUACION" =~ ^[0-9]+$ ]] ||
   (( N < 4 || N > 56 || REPETICIONES < 1 || LLAVE_EVALUACION >= (1 << N) )); then
    echo 'Uso: bitacora.sh [bits entre 4 y 56] [repeticiones positivas] [llave menor que 2^bits]' >&2
    exit 1
fi
FRASE="es una prueba de"
TEXTO=textos/prueba.txt
PROCESOS="1 2 4 8"
HIBRIDO_CONFIGS="1:2 1:4 1:8 2:4 4:2"
LIMITE=900
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
LOGDIR=${BITACORA_LOGDIR:-resultados/ejecuciones}
mkdir -p "$LOGDIR"

ejecutar() {
    local nombre=$1 esperada=$2
    shift 2
    local registro="$LOGDIR/$nombre.txt" salida
    printf '$' > "$registro"
    printf ' %q' "$@" >> "$registro"
    printf '\n' >> "$registro"
    if ! salida=$(timeout -k 2 "$LIMITE" "$@" 2>&1); then
        printf '%s\n' "$salida" >> "$registro"
        echo "Error al ejecutar $nombre. Revisar $registro" >&2
        exit 1
    fi
    printf '%s\n' "$salida" >> "$registro"
    if ! grep -qx "Llave encontrada: $esperada" <<< "$salida"; then
        echo "Error: llave inesperada en $nombre" >&2
        exit 1
    fi
    echo "Completada: $nombre" >&2
    printf '%s\n' "$salida"
}

declare -A LLAVE
LLAVE[dificil]=$LLAVE_EVALUACION
DIFICULTADES="dificil"

# Extrae "Tiempo" y las iteraciones del ganador de la salida de un programa,
# y escribe una fila del CSV.
fila() {
    local programa=$1 dificultad=$2 llave=$3 procesos=$4 hilos=$5 repeticion=$6 salida=$7
    local segundos iteraciones
    segundos=$(sed -n 's/^Tiempo: \(.*\) s/\1/p' <<< "$salida")
    iteraciones=$(sed -n 's/^Iteraciones del proceso ganador: //p' <<< "$salida")
    [ -z "$iteraciones" ] && iteraciones=$(sed -n 's/^Iteraciones: //p' <<< "$salida")
    if [ -z "$segundos" ] || [ -z "$iteraciones" ]; then
        echo "Error: resultado incompleto de $programa" >&2
        exit 1
    fi
    local semilla='' bloque=''
    [ "$programa" = permutado ] && semilla=$repeticion
    [ "$programa" = dinamico ] && bloque=4096
    echo "$programa,$dificultad,$llave,$N,$procesos,$hilos,$repeticion,$iteraciones,$segundos,$semilla,$bloque"
}

echo "programa,dificultad,llave,bits,procesos,hilos,repeticion,iter_ganador,segundos,semilla,bloque"

for dificultad in $DIFICULTADES; do
    k=${LLAVE[$dificultad]}
    cifrado="$TMP/$dificultad.bin"
    ./bin/cifrador cifrar "$TEXTO" "$cifrado" "$k" > /dev/null
    for rep in $(seq 1 "$REPETICIONES"); do
        echo "Repetición $rep de $REPETICIONES, llave $k, espacio de $N bits" >&2
        salida=$(ejecutar "secuencial_${dificultad}_${rep}" "$k" ./bin/secuencial "$cifrado" "$FRASE" "$N")
        fila secuencial "$dificultad" "$k" 1 1 "$rep" "$salida"

        for programa in naive ciclico dinamico; do
            for np in $PROCESOS; do
                salida=$(ejecutar "${programa}_${dificultad}_${np}_${rep}" "$k" mpirun --use-hwthread-cpus --bind-to none -np "$np" ./bin/"$programa" "$cifrado" "$FRASE" "$N")
                fila "$programa" "$dificultad" "$k" "$np" 1 "$rep" "$salida"
            done
        done

        # El permutado es determinista por semilla. Cada repetición usa una
        # semilla distinta para medir el tiempo esperado, no el de un orden fijo.
        for np in $PROCESOS; do
            salida=$(ejecutar "permutado_${dificultad}_${np}_${rep}" "$k" mpirun --use-hwthread-cpus --bind-to none -np "$np" ./bin/permutado "$cifrado" "$FRASE" "$N" "$rep")
            fila permutado "$dificultad" "$k" "$np" 1 "$rep" "$salida"
        done

        for config in $HIBRIDO_CONFIGS; do
            np=${config%:*}; hilos=${config#*:}
            salida=$(ejecutar "hibrido_${dificultad}_${np}x${hilos}_${rep}" "$k" env OMP_NUM_THREADS="$hilos" \
                     mpirun --use-hwthread-cpus --bind-to none -np "$np" ./bin/hibrido "$cifrado" "$FRASE" "$N")
            fila hibrido "$dificultad" "$k" "$np" "$hilos" "$rep" "$salida"
        done
    done
done
