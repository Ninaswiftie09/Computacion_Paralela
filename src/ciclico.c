/**
 * ciclico.c - Fuerza bruta DES con MPI, acercamiento alternativo 1:
 *             DISTRIBUCION CICLICA (round-robin) de llaves.
 *
 * El proceso r prueba las llaves r, r+p, r+2p, ... (p = numero de procesos).
 * Asi, una llave k siempre se encuentra tras ~k/p iteraciones sin importar
 * en que region del espacio caiga: el tiempo paralelo deja de depender de
 * "en que segmento le toco a la llave" y el speedup es ~p para toda llave.
 *
 * Terminacion anticipada: cada proceso deja un MPI_Irecv abierto para el aviso
 * "llave encontrada" y lo consulta con MPI_Test cada CHECK_EVERY iteraciones.
 * El proceso que encuentra la llave la envia a todos con MPI_Send.
 *
 * Uso: mpirun -np P ./ciclico <cifrado.bin> "<frase>" [bits_espacio]
 */
#include "des.h"
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_EVERY 4096
#define TAG_FOUND 1

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank, p;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &p);

    int bad = 0;
    unsigned char *cipher = NULL, *scratch = NULL;
    long len = 0;
    int bits = DES_BITS_LLAVE;
    if (argc < 3 || argc > 4) bad = 1;
    else {
        if (argc == 4) bits = atoi(argv[3]);
        if (bits < 1 || bits > DES_BITS_LLAVE) bad = 1;
        FILE *f = bad ? NULL : fopen(argv[1], "rb");
        if (!f) bad = 1;
        else {
            fseek(f, 0, SEEK_END); len = ftell(f); rewind(f);
            if (len <= 0 || len % DES_TAM_BLOQUE) bad = 1;
            else {
                cipher = calloc(len + 1, 1); scratch = malloc(len + 1);
                if (!cipher || !scratch || fread(cipher, 1, len, f) != (size_t)len) bad = 1;
            }
            fclose(f);
        }
    }
    if (bad) {   /* todos los procesos fallan juntos: evita bloqueos */
        if (rank == 0) fprintf(stderr, "Uso: mpirun -np P %s <cifrado.bin> \"<frase>\" [bits<=56]\n(archivo invalido, vacio o no multiplo de 8)\n", argv[0]);
        free(cipher); free(scratch);
        MPI_Finalize();
        return 1;
    }
    const char *kw = argv[2];
    uint64_t limit = ((uint64_t)1) << bits;

    uint64_t signal_buf = 0;
    MPI_Request req;
    MPI_Irecv(&signal_buf, 1, MPI_UINT64_T, MPI_ANY_SOURCE, TAG_FOUND, MPI_COMM_WORLD, &req);

    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();
    uint64_t found = 0, my_iters = 0;
    int i_found = 0, stop = 0;

    for (uint64_t k = (uint64_t)rank; k < limit && !stop; k += (uint64_t)p) {
        my_iters++;
        if (des_probar_llave(k, cipher, len, kw, scratch)) {
            found = k; i_found = 1;
            for (int d = 0; d < p; d++)
                if (d != rank) MPI_Send(&found, 1, MPI_UINT64_T, d, TAG_FOUND, MPI_COMM_WORLD);
            break;
        }
        if (my_iters % CHECK_EVERY == 0) MPI_Test(&req, &stop, MPI_STATUS_IGNORE);
    }
    double my_time = MPI_Wtime() - t0;

    if (i_found) {                      /* ya se envio el aviso: cerrar el Irecv propio */
        MPI_Cancel(&req); MPI_Wait(&req, MPI_STATUS_IGNORE);
    } else if (!stop) {                 /* termino su rango sin encontrar: esperar aviso o fin global */
        MPI_Test(&req, &stop, MPI_STATUS_IGNORE);
    }
    if (!i_found && stop) found = signal_buf;
    if (!i_found && !stop) { MPI_Cancel(&req); MPI_Wait(&req, MPI_STATUS_IGNORE); }

    /* Metricas: iteraciones por proceso y tiempo maximo (= tiempo paralelo) */
    uint64_t *all_iters = rank == 0 ? malloc(p * sizeof(uint64_t)) : NULL;
    MPI_Gather(&my_iters, 1, MPI_UINT64_T, all_iters, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
    double tmax;
    MPI_Reduce(&my_time, &tmax, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    int any = i_found, anyfound;
    MPI_Reduce(&any, &anyfound, 1, MPI_INT, MPI_MAX, 0, MPI_COMM_WORLD);
    uint64_t fkey = i_found ? found : 0, key_all;
    MPI_Reduce(&fkey, &key_all, 1, MPI_UINT64_T, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        uint64_t maxit = 0;
        for (int r = 0; r < p; r++) if (all_iters[r] > maxit) maxit = all_iters[r];
        printf("Acercamiento: CICLICO | procesos: %d | archivo: %s | frase: \"%s\"\n", p, argv[1], kw);
        if (anyfound) {
            des_descifrar(key_all, cipher, len);
            printf("Llave encontrada: %llu\nTexto: \"%s\"\n", (unsigned long long)key_all, cipher);
        } else printf("Llave NO encontrada en 2^%d llaves\n", bits);
        printf("Iteraciones por proceso:");
        for (int r = 0; r < p; r++) printf(" %llu", (unsigned long long)all_iters[r]);
        printf("\nIteraciones del proceso ganador: %llu (T_par ideal en iteraciones)\n",
               (unsigned long long)(anyfound ? (key_all / p + 1) : maxit));
        printf("Max iteraciones (incluye sobrecosto por revisar el aviso cada %d): %llu | T_par (tiempo): %.4f s\n",
               CHECK_EVERY, (unsigned long long)maxit, tmax);
        free(all_iters);
    }
    free(cipher); free(scratch);
    MPI_Finalize();
    return 0;
}
