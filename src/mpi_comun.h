/*
 * mpi_comun.h
 * Rutinas que comparten los buscadores con MPI: arranque, aviso de llave
 * encontrada, cronómetro y cierre con el reporte de resultados.
 *
 * Flujo de mensajes de una búsqueda:
 *   1. Cada proceso deja abierta una recepción con MPI_Irecv y sigue
 *      probando llaves sin bloquearse.
 *   2. Cada INTERVALO_REVISION llaves consulta con MPI_Test si ya llegó
 *      un aviso. Si llegó, deja de buscar.
 *   3. El proceso que encuentra la llave la envía con MPI_Send a los demás.
 *   4. Al terminar, MPI_Allreduce cuenta cuántos procesos encontraron llave.
 *   5. Los procesos que esperan un aviso cierran su recepción con MPI_Wait.
 *      Los que no esperan ninguno la retiran con MPI_Cancel.
 *
 * El paso 4 evita que un proceso se quede esperando un aviso que nadie va
 * a enviar, que es lo que pasa cuando la llave no está en el espacio.
 */
#ifndef MPI_COMUN_H
#define MPI_COMUN_H

#include "comun.h"

#include <mpi.h>

/* Cada cuántas llaves se consulta si otro proceso ya encontró la llave. */
#define INTERVALO_REVISION 1024

/*
 * Código con el que termina todo proceso de MPI, haya llave o no. mpirun
 * toma cualquier otro código como un fallo del trabajo y aborta a los
 * demás procesos. El resultado y los errores se informan en pantalla.
 */
#define SALIDA_MPI 0

/* Recepción pendiente del aviso de llave encontrada. */
typedef struct {
    MPI_Request solicitud;   /* recepción abierta con MPI_Irecv */
    uint64_t llave_recibida; /* llave que anunció otro proceso */
} Aviso;

/* Lo que obtuvo un proceso en su parte de la búsqueda. */
typedef struct {
    int encontrada;       /* 1 si este proceso encontró la llave */
    uint64_t llave;       /* llave que encontró */
    uint64_t iteraciones; /* llaves que probó */
    double segundos;      /* duración de su búsqueda */
} ResultadoLocal;

/*
 * Inicia MPI y deja la búsqueda lista en todos los procesos. El proceso 0
 * valida los argumentos, carga el archivo cifrado y reparte los datos.
 * Si algo falla, todos los procesos terminan MPI.
 *
 * Entradas:
 *   argc, argv         argumentos del programa
 *   uso                texto de ayuda para cuando faltan o sobran argumentos
 *   argumentos_extra   cuántos argumentos opcionales admite después de bits
 * Salidas:
 *   busqueda           datos listos para buscar en este proceso
 *   devuelve 1 si se puede buscar, 0 si no
 */
int mpi_preparar_busqueda(int *argc, char ***argv, const char *uso, int argumentos_extra,
                          Busqueda *busqueda);

/*
 * Imprime los datos de la búsqueda una sola vez, desde el proceso 0.
 *
 * Entradas:
 *   busqueda  búsqueda preparada
 *   programa  nombre del programa
 */
void mpi_imprimir_datos(const Busqueda *busqueda, const char *programa);

/*
 * Abre la recepción del aviso sin bloquear al proceso (MPI_Irecv).
 *
 * Salida:
 *   aviso  recepción pendiente
 */
void aviso_abrir(Aviso *aviso);

/*
 * Consulta si ya llegó un aviso, sin esperar (MPI_Test).
 *
 * Entradas:
 *   aviso  recepción abierta con aviso_abrir
 * Salida:
 *   1 si otro proceso ya encontró la llave, 0 si no
 */
int aviso_llego(Aviso *aviso);

/*
 * Envía la llave encontrada a los demás procesos (MPI_Send).
 *
 * Entradas:
 *   busqueda  búsqueda en curso
 *   llave     llave que encontró este proceso
 */
void aviso_enviar(const Busqueda *busqueda, uint64_t llave);

/*
 * Alinea el arranque de todos los procesos y lee el reloj de MPI.
 *
 * Salida:
 *   instante de inicio en segundos
 */
double mpi_iniciar_cronometro(void);

/*
 * Cierra la búsqueda en todos los procesos: cuenta los hallazgos, cierra la
 * recepción del aviso, reúne las métricas, imprime el resultado desde el
 * proceso 0, libera la memoria y termina MPI.
 *
 * Entradas:
 *   busqueda  búsqueda en curso
 *   aviso     recepción abierta con aviso_abrir
 *   local     resultado de este proceso
 */
void mpi_cerrar_busqueda(Busqueda *busqueda, Aviso *aviso, const ResultadoLocal *local);

#endif
