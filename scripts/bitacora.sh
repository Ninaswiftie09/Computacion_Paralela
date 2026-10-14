#!/bin/bash
# BitÃƒÂ¡cora de tiempos. Escala el espacio a 2^N y mide cada programa con
# llaves de distinta dificultad y distinto nÃƒÂºmero de procesos. Repite cada
# caso varias veces y escribe una fila por corrida en formato CSV.
#
# Uso: scripts/bitacora.sh [N] [repeticiones] > resultados/bitacora.csv
#   N             bits del espacio de llaves. Por omisiÃƒÂ³n, 24.
#   repeticiones  corridas por caso. Por omisiÃƒÂ³n, 5.
#
# Las llaves siguen las definiciones de la guÃƒÂ­a, escaladas a S = 2^N:
#   facil        S/2 + 1
#   media        S/2 + S/8
#   dificil      techo(S/7 + S/13)
#   naive_mejor  S/4 + 1   (primera del segundo proceso con 4 procesos)
#   naive_peor   S/4 - 1   (ÃƒÂºltima del primer proceso con 4 procesos)
set -euo pipefail
cd "$(dirname "$0")/.." || exit 1

N=${1:-22}
REPETICIONES=${2:-5}
FRASE="es una prueba de"
TEXTO=textos/prueba.txt
PROCESOS="1 2 4 8"
HIBRIDO_CONFIGS="1:2 1:4 1:8 2:4 4:2"
LIMITE=180
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
LOGDIR=${BITACORA_LOGDIR:-resultados/ejecuciones}
mkdir -p "$LOGDIR"

ejecutar() {
    local nombre=$1 esperada=$2
    shift 2
    local registro="$LOGDIR/$nombre.txt" salida
    printf '$ ' > "$registro"
    printf '%q ' "$@" >> "$registro"
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
    printf '%s\n' "$salida"
}

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
        salida=$(ejecutar "secuencial_${dificultad}_${rep}" "$k" ./bin/secuencial "$cifrado" "$FRASE" "$N")
        fila secuencial "$dificultad" "$k" 1 1 "$rep" "$salida"

        for programa in naive ciclico dinamico; do
            for np in $PROCESOS; do
                salida=$(ejecutar "${programa}_${dificultad}_${np}_${rep}" "$k" mpirun --use-hwthread-cpus --bind-to none -np "$np" ./bin/"$programa" "$cifrado" "$FRASE" "$N")
                fila "$programa" "$dificultad" "$k" "$np" 1 "$rep" "$salida"
            done
        done

        # El permutado es determinista por semilla. Cada repeticiÃƒÂ³n usa una
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
