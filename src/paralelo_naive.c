/*
 * paralelo_naive.c
 * ----------------
 * Version paralela "naive": el espacio de llaves [0, llave_max] se
 * divide en tantos bloques CONTIGUOS como procesos (P), cada proceso
 * "rank" recibe el bloque [rank*chunk, (rank+1)*chunk). Este es el
 * acercamiento "de ejemplo" que discute el enunciado: es sensible a en
 * qu bloque cae la llave real (si cae al inicio del bloque de un
 * proceso el speedup aparente es enorme y engaoso; si cae al final,
 * el speedup es ~1).
 *
 * Flujo de comunicacion MPI usado (ver tambien README.md):
 *
 *  1. Al iniciar, CADA proceso posta un MPI_Irecv NO bloqueante,
 *     escuchando de MPI_ANY_SOURCE con la etiqueta FOUND_TAG. Esta
 *     escucha queda "pendiente" en segundo plano mientras el proceso
 *     sigue probando llaves: es la forma de que un proceso se entere
 *     de que OTRO proceso ya encontro la respuesta, sin tener que
 *     bloquearse a esperar.
 *  2. En cada iteracion del ciclo de busqueda se hace un MPI_Test (no
 *     bloqueante) sobre ese Irecv, para revisar si ya llego el aviso de
 *     otro proceso. Si aun no ha llegado nada, se sigue probando la
 *     siguiente llave del bloque propio.
 *  3. Si un proceso ENCUENTRA la llave el mismo, hace un MPI_Send
 *     (bloqueante, mensaje pequeno de 1 entero) hacia cada uno de los
 *     demas procesos, avisandoles que ya pueden detener su busqueda.
 *  4. MPI_Wait se usa en dos momentos: (a) si el proceso encontro la
 *     llave el mismo, su propio Irecv nunca va a recibir nada (nadie le
 *     escribe a si mismo), asi que se cancela con MPI_Cancel y se hace
 *     MPI_Wait para completar formalmente esa solicitud cancelada;
 *     (b) si un proceso termina de recorrer su bloque completo SIN
 *     encontrar la llave y SIN haber recibido aviso todavia, se queda
 *     bloqueado en MPI_Wait sobre su Irecv, esperando a que algun otro
 *     proceso (que si la tenga) le avise. Esto es correcto porque se
 *     garantiza que la llave esta en algun bloque del espacio total.
 *
 * Uso:
 *   mpirun -np <P> ./paralelo_naive <archivo_cifrado> <palabra_clave> <llave_max>
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <mpi.h>
#include "des_lib.h"
#include "util.h"

#define FOUND_TAG   100
#define RESULT_TAG  101

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

    /* --- Division "naive" del espacio de llaves en bloques contiguos --- */
    unsigned long total_llaves = key_max + 1;
    unsigned long chunk = total_llaves / (unsigned long)size;
    unsigned long resto = total_llaves % (unsigned long)size;

    /* Los primeros "resto" procesos reciben un bloque de tamano chunk+1
     * para repartir el residuo lo mas equitativamente posible. */
    unsigned long start, end;
    if ((unsigned long)rank < resto) {
        start = (unsigned long)rank * (chunk + 1);
        end   = start + chunk; /* inclusive */
    } else {
        start = resto * (chunk + 1) + ((unsigned long)rank - resto) * chunk;
        end   = start + chunk - 1; /* inclusive */
    }
    if (chunk == 0 && (unsigned long)rank >= resto) {
        /* Mas procesos que llaves: este proceso no tiene nada que hacer */
        end = start - 1; /* rango vacio */
    }

    /* --- Paso 1: posta el Irecv no bloqueante (ver explicacion arriba) --- */
    int aviso_recibido = 0;
    MPI_Request recv_req;
    MPI_Irecv(&aviso_recibido, 1, MPI_INT, MPI_ANY_SOURCE, FOUND_TAG,
              MPI_COMM_WORLD, &recv_req);

    char *descifrado = malloc(len + 1);
    unsigned long found_key = ULONG_MAX; /* ULONG_MAX = "no encontrada" */

    MPI_Barrier(MPI_COMM_WORLD); /* arrancar todos al mismo tiempo */
    double t0 = MPI_Wtime();

    for (unsigned long k = start; k <= end && start <= end; k++) {
        if (try_key(k, buf, len, keyword, descifrado)) {
            found_key = k;
            /* --- Paso 3: avisar a todos los demas procesos --- */
            int dummy = 1;
            for (int p = 0; p < size; p++) {
                if (p != rank) {
                    MPI_Send(&dummy, 1, MPI_INT, p, FOUND_TAG, MPI_COMM_WORLD);
                }
            }
            break;
        }

        /* --- Paso 2: revisar (sin bloquear) si otro proceso ya avisó --- */
        int flag = 0;
        MPI_Test(&recv_req, &flag, MPI_STATUS_IGNORE);
        if (flag) {
            break; /* otro proceso ya encontro la llave */
        }
    }

    /* --- Paso 4: completar/cerrar correctamente el Irecv pendiente --- */
    if (found_key != ULONG_MAX) {
        /* La encontramos nosotros: nadie nos va a escribir, cancelamos
         * nuestra propia escucha pendiente. */
        MPI_Cancel(&recv_req);
        MPI_Wait(&recv_req, MPI_STATUS_IGNORE);
    } else {
        /* Puede que ya haya llegado el aviso (MPI_Test lo detecto arriba,
         * en cuyo caso el request ya esta completo y este Wait retorna
         * de inmediato), o puede que todavia no: en ese caso bloqueamos
         * hasta que llegue (la llave esta garantizada en algun bloque). */
        MPI_Wait(&recv_req, MPI_STATUS_IGNORE);
    }

    double t1 = MPI_Wtime();
    double elapsed = t1 - t0;

    /* --- Recolectar resultados en el proceso 0 --- */
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
        printf("=== Resultado busqueda paralela (naive, bloques contiguos) ===\n");
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
