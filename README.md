# Proyecto 2: búsqueda de llave DES con MPI

Computación Paralela y Distribuida, UVG, Sección 20.

| Integrante | Carné |
|---|---|
| Roberto Barreda | 23354 |
| Nina Najera | 231088 |
| Jose Anton | 221041 |

Repositorio: https://github.com/Ninaswiftie09/Computacion_Paralela/tree/Proyecto2

## Requisitos y compilación

Linux o WSL con GCC, Make, Open MPI, OpenSSL y Python 3.

```bash
sudo apt install build-essential openmpi-bin libopenmpi-dev libssl-dev python3-matplotlib python3-pandas python3-numpy
make
```

También se puede usar el contenedor local de pruebas:

```bash
docker build -t proyecto2-mpi:local .
docker run --rm -it --mount "type=bind,source=$PWD,target=/proyecto" proyecto2-mpi:local
```

Los comandos siguientes se ejecutan desde la raíz del repositorio. Con Docker, se ejecutan dentro del contenedor.

## Ejecución

```bash
./bin/cifrador cifrar textos/prueba.txt textos/prueba.bin 42
./bin/cifrador descifrar textos/prueba.bin 42
./bin/secuencial textos/prueba.bin "es una prueba de" 24
mpirun --use-hwthread-cpus --bind-to none -np 4 ./bin/naive textos/prueba.bin "es una prueba de" 24
mpirun --use-hwthread-cpus --bind-to none -np 4 ./bin/ciclico textos/prueba.bin "es una prueba de" 24
mpirun --use-hwthread-cpus --bind-to none -np 4 ./bin/permutado textos/prueba.bin "es una prueba de" 24 1
mpirun --use-hwthread-cpus --bind-to none -np 4 ./bin/dinamico textos/prueba.bin "es una prueba de" 24 4096
OMP_NUM_THREADS=2 mpirun --use-hwthread-cpus --bind-to none -np 4 ./bin/hibrido textos/prueba.bin "es una prueba de" 24
```

El argumento bits limita la búsqueda a las llaves entre 0 y 2^bits - 1. Sin él se usa el espacio de 56 bits. No cambia el algoritmo DES ni reduce la llave interna del cifrador.

| Programa | Reparto |
|---|---|
| cifrador | Cifra un archivo o muestra su descifrado |
| secuencial | Recorre las llaves en un proceso |
| naive | Asigna un rango contiguo a cada proceso |
| ciclico | El proceso r prueba r, r+P, r+2P |
| permutado | Recorre una permutación del espacio, con semilla opcional después de bits |
| dinamico | El proceso 0 entrega bloques a P-1 trabajadores. El tamaño opcional sigue a bits y vale 4096 por defecto |
| hibrido | Combina reparto cíclico MPI con hilos OpenMP |

Con un proceso, el dinámico usa búsqueda secuencial. Con varios, el proceso 0 coordina y no prueba llaves. Una detención puede tardar lo que reste del bloque activo.

## Verificación y mediciones

```bash
bash scripts/evidencias.sh
bash scripts/bitacora.sh 22 5 > resultados/bitacora.csv
python3 scripts/validar_bitacora.py
python3 scripts/graficas.py
python3 scripts/catalogo.py
```

El primer script recompila, registra el entorno, prueba entradas y recuperación de llaves, ejecuta B.1/B.2 y contrasta el cifrado con OpenSSL. B.2 usa cuatro procesos y registra un límite de 20 segundos para el caso que no termina.

La bitácora usa cinco tipos de llave, cinco repeticiones y 1, 2, 4 y 8 procesos. Incluye cinco configuraciones híbridas. Cada fila guarda llave, bits, procesos, hilos, tiempo, semilla y tamaño de bloque cuando corresponden. Las salidas completas quedan en resultados/ejecuciones/. Las tablas y gráficas se calculan con los datos de esa ejecución. No se deben combinar mediciones de entornos distintos en un mismo speedup.

El tiempo mide la búsqueda y la detención de los procesos. Excluye la carga del archivo y las operaciones finales que reúnen e imprimen resultados. La comparación de algoritmos se hace con el mismo texto, frase y espacio. En el permutado se cambia la semilla entre repeticiones para observar la variación del recorrido.

## Sincronización

MPI tiene una barrera explícita de arranque. El cierre de mensajes espera todas las recepciones y envíos pendientes. El híbrido usa una bandera atómica, contadores por hilo y una barrera explícita antes de reunirlos. No usa ciclos OpenMP ni reducciones con barreras implícitas. OpenMP exige la unión de los hilos al salir de una región parallel, y esa unión no admite nowait.

## Informe

El informe de entrega es docs/informe/informe.tex. Se compila con pdfLaTeX dos veces desde docs/informe/. Las carpetas img/, tablas/ y capturas/ deben permanecer junto al archivo al subirlo a Overleaf. Las capturas corresponden a registros de ejecuciones reales. El Word conserva una versión anterior y no es la versión final de entrega.
