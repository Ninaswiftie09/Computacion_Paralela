/*
 * ciclico.c
 * Búsqueda de la llave con MPI, acercamiento de distribución cíclica.
 * Con P procesos, el proceso r prueba las llaves r, r + P, r + 2P y
 * siguientes. Entre todos recorren el espacio en el mismo orden que la
 * versión secuencial, así que la llave k aparece tras k / P iteraciones
 * sin importar en qué parte del espacio esté.
 *
 * Uso: mpirun -np <procesos> ciclico <cifrado.bin> "<frase>" [bits]
 *   bits  se prueban las llaves menores que 2^bits. Por omisión, 56.
 */
#include "comun.h"
#include "des.h"
#include "mpi_comun.h"

static const char USO[] = "Uso: mpirun -np <procesos> ciclico <cifrado.bin> \"<frase>\" [bits]";

int main(int argc, char **argv)
{
    Busqueda busqueda;

    if (!mpi_preparar_busqueda(&argc, &argv, USO, 0, &busqueda)) {
        return SALIDA_MPI;
    }
    mpi_imprimir_datos(&busqueda, "ciclico");

    /* Este proceso empieza en su propio número y salta de P en P. */
    uint64_t primera_llave = (uint64_t)busqueda.id_proceso;
    uint64_t salto = (uint64_t)busqueda.num_procesos;

    Aviso aviso;
    ResultadoLocal local = {0, 0, 0, 0.0};
    aviso_abrir(&aviso);
    double inicio = mpi_iniciar_cronometro();

    for (uint64_t llave = primera_llave; llave < busqueda.total_llaves; llave += salto) {
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
