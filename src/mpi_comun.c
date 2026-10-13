/*
 * mpi_comun.c
 * Implementación de mpi_comun.h.
 */
#include "mpi_comun.h"

#include "des.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROCESO_RAIZ 0
#define ETIQUETA_LLAVE 1
#define SIN_GANADOR (-1)

/* Posiciones de la cabecera que el proceso 0 reparte antes de los datos. */
enum { CAB_VALIDA, CAB_BITS, CAB_LONGITUD, CAB_BYTES_FRASE, CAB_TOTAL };

/*
 * Reserva en un proceso distinto del 0 la memoria que el proceso 0 ya
 * reservó al cargar el archivo.
 *
 * Entradas:
 *   busqueda     búsqueda con la longitud ya definida
 *   bytes_frase  bytes de la frase con su cero final
 * Salida:
 *   1 si hubo memoria, 0 si no
 */
static int reservar_memoria(Busqueda *busqueda, size_t bytes_frase)
{
    busqueda->cifrado = calloc(busqueda->longitud + 1, 1);
    busqueda->trabajo = malloc(busqueda->longitud + 1);
    busqueda->frase = malloc(bytes_frase);
    return busqueda->cifrado != NULL && busqueda->trabajo != NULL && busqueda->frase != NULL;
}

/*
 * Cierra la recepción del aviso según los mensajes que faltan por llegar.
 *
 * Entradas:
 *   aviso             recepción abierta con aviso_abrir
 *   avisos_esperados  procesos distintos de este que encontraron llave
 */
static void aviso_cerrar(Aviso *aviso, int avisos_esperados)
{
    if (avisos_esperados == 0) {
        /* Nadie le va a escribir a este proceso, así que retira su recepción. */
        if (aviso->solicitud != MPI_REQUEST_NULL) {
            MPI_Cancel(&aviso->solicitud);
        }
        MPI_Wait(&aviso->solicitud, MPI_STATUS_IGNORE);
        return;
    }

    /* Espera el aviso si todavía no había llegado. */
    MPI_Wait(&aviso->solicitud, MPI_STATUS_IGNORE);

    /* Con frases cortas puede haber más de un hallazgo. Se reciben los sobrantes. */
    for (int sobrante = 1; sobrante < avisos_esperados; sobrante++) {
        uint64_t descartada = 0;
        MPI_Recv(&descartada, 1, MPI_UINT64_T, MPI_ANY_SOURCE, ETIQUETA_LLAVE, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
    }
}

int mpi_preparar_busqueda(int *argc, char ***argv, const char *uso, int argumentos_extra,
                          Busqueda *busqueda)
{
    int id_proceso = 0;
    int num_procesos = 1;
    int cabecera[CAB_TOTAL] = {0};

    MPI_Init(argc, argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &id_proceso);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procesos);
    memset(busqueda, 0, sizeof *busqueda);

    /* Solo el proceso 0 valida y lee el archivo. Así todos reciben el mismo veredicto. */
    if (id_proceso == PROCESO_RAIZ) {
        int cantidad = *argc;
        char **argumentos = *argv;

        if (cantidad < 3 || cantidad > 4 + argumentos_extra) {
            fprintf(stderr, "%s\n", uso);
        } else {
            const char *texto_bits = cantidad >= 4 ? argumentos[3] : NULL;
            cabecera[CAB_VALIDA] =
                busqueda_preparar(argumentos[1], argumentos[2], texto_bits, busqueda);
        }
        if (cabecera[CAB_VALIDA]) {
            cabecera[CAB_BITS] = busqueda->bits_espacio;
            cabecera[CAB_LONGITUD] = (int)busqueda->longitud;
            cabecera[CAB_BYTES_FRASE] = (int)strlen(busqueda->frase) + 1;
        }
    }
    MPI_Bcast(cabecera, CAB_TOTAL, MPI_INT, PROCESO_RAIZ, MPI_COMM_WORLD);
    if (!cabecera[CAB_VALIDA]) {
        MPI_Finalize();
        return 0;
    }

    int hay_memoria = 1;
    if (id_proceso != PROCESO_RAIZ) {
        busqueda->ruta_cifrado = (*argv)[1];
        busqueda->bits_espacio = cabecera[CAB_BITS];
        busqueda->total_llaves = UINT64_C(1) << cabecera[CAB_BITS];
        busqueda->longitud = (size_t)cabecera[CAB_LONGITUD];
        hay_memoria = reservar_memoria(busqueda, (size_t)cabecera[CAB_BYTES_FRASE]);
    }
    busqueda->id_proceso = id_proceso;
    busqueda->num_procesos = num_procesos;

    /* Si a un proceso le faltó memoria, todos se enteran y terminan juntos. */
    int todos_con_memoria = 0;
    MPI_Allreduce(&hay_memoria, &todos_con_memoria, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    if (!todos_con_memoria) {
        if (id_proceso == PROCESO_RAIZ) {
            fprintf(stderr, "Error: un proceso no tuvo memoria para preparar la búsqueda\n");
        }
        busqueda_liberar(busqueda);
        MPI_Finalize();
        return 0;
    }

    MPI_Bcast(busqueda->cifrado, cabecera[CAB_LONGITUD], MPI_UNSIGNED_CHAR, PROCESO_RAIZ,
              MPI_COMM_WORLD);
    MPI_Bcast(busqueda->frase, cabecera[CAB_BYTES_FRASE], MPI_CHAR, PROCESO_RAIZ, MPI_COMM_WORLD);
    return 1;
}

void mpi_imprimir_datos(const Busqueda *busqueda, const char *programa)
{
    if (busqueda->id_proceso == PROCESO_RAIZ) {
        imprimir_datos_busqueda(busqueda, programa);
    }
}

void aviso_abrir(Aviso *aviso)
{
    aviso->llave_recibida = 0;
    MPI_Irecv(&aviso->llave_recibida, 1, MPI_UINT64_T, MPI_ANY_SOURCE, ETIQUETA_LLAVE,
              MPI_COMM_WORLD, &aviso->solicitud);
}

int aviso_llego(Aviso *aviso)
{
    int llego = 0;

    MPI_Test(&aviso->solicitud, &llego, MPI_STATUS_IGNORE);
    return llego;
}

void aviso_enviar(const Busqueda *busqueda, uint64_t llave)
{
    for (int destino = 0; destino < busqueda->num_procesos; destino++) {
        if (destino != busqueda->id_proceso) {
            MPI_Send(&llave, 1, MPI_UINT64_T, destino, ETIQUETA_LLAVE, MPI_COMM_WORLD);
        }
    }
}

double mpi_iniciar_cronometro(void)
{
    MPI_Barrier(MPI_COMM_WORLD);
    return MPI_Wtime();
}

void mpi_cerrar_busqueda(Busqueda *busqueda, Aviso *aviso, const ResultadoLocal *local)
{
    int es_raiz = busqueda->id_proceso == PROCESO_RAIZ;

    /* Cuántos procesos encontraron una llave. */
    int hallazgos = 0;
    MPI_Allreduce(&local->encontrada, &hallazgos, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    aviso_cerrar(aviso, hallazgos - local->encontrada);

    /* Si hubo más de un hallazgo se reporta la menor llave. */
    uint64_t llave_local = local->encontrada ? local->llave : UINT64_MAX;
    uint64_t llave_global = UINT64_MAX;
    MPI_Allreduce(&llave_local, &llave_global, 1, MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);

    int es_ganador = local->encontrada && local->llave == llave_global;
    int candidato = es_ganador ? busqueda->id_proceso : SIN_GANADOR;
    int ganador = SIN_GANADOR;
    MPI_Allreduce(&candidato, &ganador, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);

    /* El tiempo paralelo es el del proceso que más tardó en detenerse. */
    double segundos = 0.0;
    MPI_Reduce(&local->segundos, &segundos, 1, MPI_DOUBLE, MPI_MAX, PROCESO_RAIZ, MPI_COMM_WORLD);

    uint64_t *iteraciones = NULL;
    if (es_raiz) {
        iteraciones = malloc((size_t)busqueda->num_procesos * sizeof *iteraciones);
        if (iteraciones == NULL) {
            fprintf(stderr, "Error: no hay memoria para reunir las iteraciones\n");
            MPI_Abort(MPI_COMM_WORLD, SALIDA_ERROR);
        }
    }
    MPI_Gather(&local->iteraciones, 1, MPI_UINT64_T, iteraciones, 1, MPI_UINT64_T, PROCESO_RAIZ,
               MPI_COMM_WORLD);

    if (es_raiz) {
        uint64_t total_iteraciones = 0;

        imprimir_llave(busqueda, hallazgos > 0, llave_global);
        if (hallazgos > 1) {
            printf("Aviso: %d procesos encontraron una llave posible. Se muestra la menor.\n",
                   hallazgos);
        }
        if (hallazgos > 0) {
            printf("Proceso que la encontró: %d\n", ganador);
        }
        printf("Iteraciones por proceso:");
        for (int proceso = 0; proceso < busqueda->num_procesos; proceso++) {
            printf(" %" PRIu64, iteraciones[proceso]);
            total_iteraciones += iteraciones[proceso];
        }
        printf("\n");
        if (hallazgos > 0) {
            printf("Iteraciones del proceso ganador: %" PRIu64 "\n", iteraciones[ganador]);
        }
        imprimir_tiempo(segundos, total_iteraciones);
        free(iteraciones);
    }

    busqueda_liberar(busqueda);
    MPI_Finalize();
}
