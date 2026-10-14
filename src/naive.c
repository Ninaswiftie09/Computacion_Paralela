/*
 * naive.c
 * Búsqueda de la llave con MPI, acercamiento naive de bruteforce.c.
 * El espacio de llaves se parte en un rango contiguo por proceso y cada
 * proceso recorre el suyo en orden.
 *
 * Uso: mpirun -np <procesos> naive <cifrado.bin> "<frase>" [bits]
 *   bits  se prueban las llaves menores que 2^bits. Por omisión, 56.
 */
#include "comun.h"
#include "des.h"
#include "mpi_comun.h"

static const char USO[] = "Uso: mpirun -np <procesos> naive <cifrado.bin> \"<frase>\" [bits]";

int main(int argc, char **argv)
{
    Busqueda busqueda;

    if (!mpi_preparar_busqueda(&argc, &argv, USO, 0, &busqueda)) {
        return SALIDA_MPI;
    }
    mpi_imprimir_datos(&busqueda, "naive");

    /* Rango contiguo de este proceso. El último se queda con el residuo. */
    uint64_t llaves_por_proceso = busqueda.total_llaves / (uint64_t)busqueda.num_procesos;
    uint64_t primera_llave = llaves_por_proceso * (uint64_t)busqueda.id_proceso;
    uint64_t limite = primera_llave + llaves_por_proceso;
    if (busqueda.id_proceso == busqueda.num_procesos - 1) {
        limite = busqueda.total_llaves;
    }

    Aviso aviso;
    ResultadoLocal local = {0, 0, 0, 0.0};
    aviso_abrir(&aviso);
    double inicio = mpi_iniciar_cronometro();

    for (uint64_t llave = primera_llave; llave < limite; llave++) {
        local.iteraciones++;
        if (des_probar_llave(llave, busqueda.cifrado, busqueda.longitud, busqueda.frase,
                             busqueda.trabajo)) {
            local.encontrada = 1;
            local.llave = llave;
            aviso_enviar(&busqueda, &aviso, llave);
            break;
        }
        if (local.iteraciones % INTERVALO_REVISION == 0 && aviso_llego(&aviso)) {
            break;
        }
    }
    local.segundos = MPI_Wtime() - inicio;

    mpi_cerrar_busqueda(&busqueda, &aviso, &local);
    return SALIDA_MPI;
}
