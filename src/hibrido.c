/*
 * hibrido.c
 * Búsqueda de la llave con MPI y OpenMP. Cada proceso de MPI reparte su
 * trabajo entre varios hilos de OpenMP, así se aprovecha la memoria
 * compartida de un nodo sin abrir un proceso por núcleo.
 *
 * El reparto es cíclico entre los P procesos y los T hilos a la vez. Con
 * W = P * T trabajadores, el trabajador de índice global g prueba las
 * llaves g, g + W, g + 2W y siguientes. Cada llave le toca a un solo
 * trabajador y entre todos cubren el espacio.
 *
 * MPI se inicia en modo FUNNELED, así que solo el hilo principal llama a
 * MPI. Los demás hilos solo prueban llaves. La detención anticipada usa
 * una bandera compartida entre los hilos, y el hilo principal consulta de
 * vez en cuando si otro proceso ya encontró la llave.
 *
 * Uso: OMP_NUM_THREADS=<hilos> mpirun -np <procesos> hibrido <cifrado.bin> "<frase>" [bits]
 *   bits  se prueban las llaves menores que 2^bits. Por omisión, 56.
 */
#include "comun.h"
#include "des.h"
#include "mpi_comun.h"

#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

#define PROCESO_RAIZ 0

static const char USO[] =
    "Uso: OMP_NUM_THREADS=<hilos> mpirun -np <procesos> hibrido <cifrado.bin> \"<frase>\" [bits]";

/*
 * Reserva un arreglo de trabajo por hilo, porque des_probar_llave escribe
 * en él y los hilos no pueden compartir el mismo.
 *
 * Entradas:
 *   hilos          cantidad de arreglos a reservar
 *   bytes_trabajo  tamaño de cada arreglo
 * Salida:
 *   arreglo de punteros, o NULL si faltó memoria
 */
static unsigned char **reservar_trabajos(int hilos, size_t bytes_trabajo)
{
    unsigned char **trabajos = calloc((size_t)hilos, sizeof *trabajos);
    if (trabajos == NULL) {
        return NULL;
    }
    for (int hilo = 0; hilo < hilos; hilo++) {
        trabajos[hilo] = malloc(bytes_trabajo);
        if (trabajos[hilo] == NULL) {
            for (int previo = 0; previo < hilo; previo++) {
                free(trabajos[previo]);
            }
            free(trabajos);
            return NULL;
        }
    }
    return trabajos;
}

static void liberar_trabajos(unsigned char **trabajos, int hilos)
{
    if (trabajos == NULL) {
        return;
    }
    for (int hilo = 0; hilo < hilos; hilo++) {
        free(trabajos[hilo]);
    }
    free(trabajos);
}

int main(int argc, char **argv)
{
    Busqueda busqueda;

    if (!mpi_preparar_busqueda(&argc, &argv, USO, 0, &busqueda)) {
        return SALIDA_MPI;
    }

    /* Todos los procesos usan la misma cantidad de hilos, para que el
     * reparto global no deje huecos ni solapes. La fija el proceso 0. */
    int hilos = busqueda.id_proceso == PROCESO_RAIZ ? omp_get_max_threads() : 0;
    MPI_Bcast(&hilos, 1, MPI_INT, PROCESO_RAIZ, MPI_COMM_WORLD);
    if (hilos < 1) {
        hilos = 1;
    }

    mpi_imprimir_datos(&busqueda, "hibrido");
    if (busqueda.id_proceso == PROCESO_RAIZ) {
        printf("Hilos por proceso: %d\n", hilos);
        fflush(stdout);
    }

    unsigned char **trabajos = reservar_trabajos(hilos, busqueda.longitud + 1);

    Aviso aviso;
    ResultadoLocal local = {0, 0, 0, 0.0};
    aviso_abrir(&aviso);

    /* Bandera de detención compartida. Es volatile para que todos los hilos
     * relean su valor. Solo pasa de 0 a 1, así que una lectura adelantada a
     * lo sumo hace unas iteraciones de más. */
    volatile int detener = trabajos == NULL;
    int encontrada = 0;
    uint64_t llave_hallada = 0;
    uint64_t iteraciones_totales = 0;
    uint64_t trabajadores = (uint64_t)busqueda.num_procesos * (uint64_t)hilos;

    double inicio = mpi_iniciar_cronometro();

    #pragma omp parallel num_threads(hilos) reduction(+ : iteraciones_totales)
    {
        int id_hilo = omp_get_thread_num();
        unsigned char *trabajo = trabajos != NULL ? trabajos[id_hilo] : NULL;
        uint64_t indice_global = (uint64_t)busqueda.id_proceso * (uint64_t)hilos + (uint64_t)id_hilo;
        uint64_t iteraciones_hilo = 0;

        for (uint64_t llave = indice_global; llave < busqueda.total_llaves && !detener;
             llave += trabajadores) {
            iteraciones_hilo++;
            if (des_probar_llave(llave, busqueda.cifrado, busqueda.longitud, busqueda.frase,
                                 trabajo)) {
                #pragma omp critical
                {
                    if (!encontrada) {
                        encontrada = 1;
                        llave_hallada = llave;
                    }
                }
                detener = 1;
                break;
            }
            /* Solo el hilo principal consulta el aviso de MPI (modo FUNNELED). */
            if (id_hilo == 0 && iteraciones_hilo % INTERVALO_REVISION == 0 && aviso_llego(&aviso)) {
                detener = 1;
                break;
            }
        }
        iteraciones_totales += iteraciones_hilo;
    }

    local.encontrada = encontrada;
    local.llave = llave_hallada;
    local.iteraciones = iteraciones_totales;
    if (encontrada) {
        aviso_enviar(&busqueda, llave_hallada);
    }
    local.segundos = MPI_Wtime() - inicio;

    liberar_trabajos(trabajos, hilos);
    mpi_cerrar_busqueda(&busqueda, &aviso, &local);
    return SALIDA_MPI;
}
