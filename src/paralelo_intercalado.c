/*
 * paralelo_intercalado.c
 * -----------------------
 * ALTERNATIVA #1 al acercamiento "naive" (bloques contiguos).
 *
 * Problema que resuelve: en la version naive, si la llave real cae al
 * inicio del bloque de un proceso el speedup medido es artificialmente
 * altisimo, y si cae al final de un bloque el speedup es ~1 (no hay
 * ganancia real). Esto hace que el speedup dependa demasiado de la
 * ubicacion exacta de la llave dentro del espacio total.
 *
 * Idea: en vez de repartir el espacio en P bloques contiguos, cada
 * proceso "rank" prueba las llaves intercaladas (round-robin):
 *
 *     rank, rank + P, rank + 2P, rank + 3P, ...
 *
 * Con esto, sin importar en que posicion absoluta este la llave real
 * dentro de [0, llave_max], el numero de iteraciones que le toca hacer
 * al proceso responsable es aproximadamente (posicion / P), igual que
 * en el caso naive -pero- el "peor caso" (llave al final del rango)
 * deja de estar concentrado siempre en el mismo proceso (el ultimo),
 * y ademas todos los procesos avanzan "al mismo ritmo" por todo el
 * rango, dando resultados de speedup mas consistentes y menos
 * dependientes del approach de particionamiento en si.
 *
 * El protocolo de comunicacion MPI (Irecv/Send/Wait) es identico al de
 * paralelo_naive.c; lo unico que cambia es como se recorre el espacio
 * de llaves.
 *
 * Uso:
 *   mpirun -np <P> ./paralelo_intercalado <archivo_cifrado> <palabra_clave> <llave_max>
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <mpi.h>
#include "des_lib.h"
#include "util.h"

#define FOUND_TAG 100

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc != 4) {
        if (rank == 0) {
            fprintf(stderr,
                "Uso: mpirun -np <P> %s <archivo_cifrado> <palabra_clave> <llave_max>\n",
                argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    const char *ciph_path = argv[1];
    const char *keyword   = argv[2];
    unsigned long key_max = strtoul(argv[3], NULL, 10);

    size_t len, cap;
    unsigned char *buf = read_file_padded(ciph_path, &len, &cap);
    if (!buf) {
        if (rank == 0) fprintf(stderr, "Error: no se pudo leer '%s'\n", ciph_path);
        MPI_Finalize();
        return 1;
    }

    int aviso_recibido = 0;
    MPI_Request recv_req;
    MPI_Irecv(&aviso_recibido, 1, MPI_INT, MPI_ANY_SOURCE, FOUND_TAG,
              MPI_COMM_WORLD, &recv_req);

    char *descifrado = malloc(len + 1);
    unsigned long found_key = ULONG_MAX;

    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();

    for (unsigned long k = (unsigned long)rank; k <= key_max; k += (unsigned long)size) {
        if (try_key(k, buf, len, keyword, descifrado)) {
            found_key = k;
            int dummy = 1;
            for (int p = 0; p < size; p++) {
                if (p != rank) MPI_Send(&dummy, 1, MPI_INT, p, FOUND_TAG, MPI_COMM_WORLD);
            }
            break;
        }

        int flag = 0;
        MPI_Test(&recv_req, &flag, MPI_STATUS_IGNORE);
        if (flag) break;
    }

    if (found_key != ULONG_MAX) {
        MPI_Cancel(&recv_req);
        MPI_Wait(&recv_req, MPI_STATUS_IGNORE);
    } else {
        MPI_Wait(&recv_req, MPI_STATUS_IGNORE);
    }

    double t1 = MPI_Wtime();
    double elapsed = t1 - t0;

    unsigned long *todas_las_llaves = NULL;
    if (rank == 0) todas_las_llaves = malloc(sizeof(unsigned long) * (size_t)size);
    MPI_Gather(&found_key, 1, MPI_UNSIGNED_LONG,
               todas_las_llaves, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);

    double max_elapsed;
    MPI_Reduce(&elapsed, &max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        int rank_ganador = -1;
        unsigned long llave_final = ULONG_MAX;
        for (int p = 0; p < size; p++) {
            if (todas_las_llaves[p] != ULONG_MAX) {
                rank_ganador = p;
                llave_final = todas_las_llaves[p];
                break;
            }
        }
        printf("=== Resultado busqueda paralela (intercalada / round-robin) ===\n");
        printf("Procesos (np): %d\n", size);
        if (rank_ganador >= 0) {
            printf("Llave encontrada: %lu (por el proceso %d)\n", llave_final, rank_ganador);
        } else {
            printf("No se encontro la llave en el rango dado.\n");
        }
        printf("Tiempo paralelo (Tpar, max entre procesos): %.6f s\n", max_elapsed);
        free(todas_las_llaves);
    }

    free(descifrado);
    free(buf);
    MPI_Finalize();
    return 0;
}
