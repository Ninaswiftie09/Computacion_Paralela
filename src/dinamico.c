/*
 * Búsqueda por bloques dinámicos. El proceso 0 entrega rangos pequeños
 * a los trabajadores. Cada trabajador devuelve su resultado y solicita
 * otro bloque. Un rango vacío indica que debe terminar.
 */
#include "comun.h"
#include "des.h"
#include "mpi_comun.h"

#include <inttypes.h>
#include <stdio.h>

#define ETIQUETA_PEDIDO 20
#define ETIQUETA_BLOQUE 21
#define BLOQUE_PREDETERMINADO UINT64_C(4096)

static const char USO[] =
    "Uso: mpirun -np <procesos> dinamico <cifrado.bin> \"<frase>\" [bits] [bloque]";

/* Entrada: estado de búsqueda y tamaño de bloque. Salida: atiende todos
 * los pedidos, sin repetir rangos y sin dejar trabajadores esperando. */
static void coordinar(const Busqueda *busqueda, uint64_t bloque)
{
    uint64_t siguiente = 0;
    int activos = busqueda->num_procesos - 1;
    int detener = 0;
    while (activos > 0) {
        uint64_t pedido[2];
        uint64_t rango[2] = {0, 0};
        MPI_Status estado;
        MPI_Recv(pedido, 2, MPI_UINT64_T, MPI_ANY_SOURCE, ETIQUETA_PEDIDO,
                 MPI_COMM_WORLD, &estado);
        if (pedido[0]) {
            detener = 1;
        }
        if (!detener && siguiente < busqueda->total_llaves) {
            uint64_t restantes = busqueda->total_llaves - siguiente;
            rango[0] = siguiente;
            rango[1] = siguiente + (restantes < bloque ? restantes : bloque);
            siguiente = rango[1];
        } else {
            activos--;
        }
        MPI_Send(rango, 2, MPI_UINT64_T, estado.MPI_SOURCE, ETIQUETA_BLOQUE,
                 MPI_COMM_WORLD);
    }
}

/* Entrada: búsqueda preparada. Salida: resultado local y llaves probadas.
 * Se informa un hallazgo al pedir el siguiente bloque. La detención de
 * los demás trabajadores tarda como máximo lo que reste de su bloque. */
static void trabajar(Busqueda *busqueda, ResultadoLocal *local)
{
    for (;;) {
        uint64_t pedido[2] = {(uint64_t)local->encontrada, local->llave};
        uint64_t rango[2];
        MPI_Send(pedido, 2, MPI_UINT64_T, 0, ETIQUETA_PEDIDO, MPI_COMM_WORLD);
        MPI_Recv(rango, 2, MPI_UINT64_T, 0, ETIQUETA_BLOQUE,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        if (rango[0] == rango[1]) {
            break;
        }
        for (uint64_t llave = rango[0]; llave < rango[1]; llave++) {
            local->iteraciones++;
            if (des_probar_llave(llave, busqueda->cifrado, busqueda->longitud,
                                 busqueda->frase, busqueda->trabajo)) {
                local->encontrada = 1;
                local->llave = llave;
                break;
            }
        }
    }
}

int main(int argc, char **argv)
{
    Busqueda busqueda;
    if (!mpi_preparar_busqueda(&argc, &argv, USO, 1, &busqueda)) {
        return SALIDA_MPI;
    }
    uint64_t bloque = BLOQUE_PREDETERMINADO;
    if (argc == 5 && (!leer_llave(argv[4], &bloque) || bloque == 0)) {
        if (busqueda.id_proceso == 0) {
            fprintf(stderr, "Error: bloque debe estar entre 1 y 2^56 - 1\n");
        }
        busqueda_liberar(&busqueda);
        MPI_Finalize();
        return SALIDA_MPI;
    }
    mpi_imprimir_datos(&busqueda, "dinamico");
    if (busqueda.id_proceso == 0) {
        printf("Llaves por bloque: %" PRIu64 "\n", bloque);
        printf("Trabajadores: %d\n", busqueda.num_procesos > 1 ? busqueda.num_procesos - 1 : 1);
    }
    Aviso aviso;
    ResultadoLocal local = {0, 0, 0, 0.0};
    aviso_abrir(&aviso);
    double inicio = mpi_iniciar_cronometro();
    if (busqueda.num_procesos == 1) {
        for (uint64_t llave = 0; llave < busqueda.total_llaves; llave++) {
            local.iteraciones++;
            if (des_probar_llave(llave, busqueda.cifrado, busqueda.longitud,
                                 busqueda.frase, busqueda.trabajo)) {
                local.encontrada = 1;
                local.llave = llave;
                break;
            }
        }
    } else if (busqueda.id_proceso == 0) {
        coordinar(&busqueda, bloque);
    } else {
        trabajar(&busqueda, &local);
    }
    if (local.encontrada) {
        aviso_enviar(&busqueda, &aviso, local.llave);
    }
    local.segundos = MPI_Wtime() - inicio;
    mpi_cerrar_busqueda(&busqueda, &aviso, &local);
    return SALIDA_MPI;
}
