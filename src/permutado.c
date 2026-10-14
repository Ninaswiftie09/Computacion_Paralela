/*
 * permutado.c
 * Búsqueda de la llave con MPI, acercamiento de recorrido permutado.
 *
 * El cíclico recorre el espacio en orden creciente, así que una llave de
 * valor bajo aparece pronto y una de valor alto aparece tarde. Aquí se
 * recorre el mismo espacio pero en un orden revuelto por una biyección
 * lineal sobre los índices:
 *
 *     candidata(j) = (A * j + C) mod 2^bits,  con A impar
 *
 * Como A es impar, es invertible módulo 2^bits y la función recorre las
 * 2^bits llaves sin repetir ninguna. El proceso r toma los índices
 * r, r + P, r + 2P y siguientes, de modo que los subconjuntos de llaves
 * son disjuntos y juntos cubren todo el espacio.
 *
 * La posición de una llave dentro del recorrido deja de coincidir con su
 * valor. Al variar la semilla se busca reducir esa dependencia. A y C se
 * derivan de una semilla que permite reproducir el mismo recorrido.
 *
 * Uso: mpirun -np <procesos> permutado <cifrado.bin> "<frase>" [bits] [semilla]
 *   bits     se prueban las llaves menores que 2^bits. Por omisión, 56.
 *   semilla  entero que fija el recorrido. Por omisión, 0.
 */
#include "comun.h"
#include "des.h"
#include "mpi_comun.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

/* Constantes del mezclador splitmix64, para repartir bien los bits. */
#define GOLDEN UINT64_C(0x9E3779B97F4A7C15)
#define MEZCLA_1 UINT64_C(0xBF58476D1CE4E5B9)
#define MEZCLA_2 UINT64_C(0x94D049BB133111EB)

#define SEMILLA_POR_DEFECTO 0
#define DESPLAZA_C UINT64_C(0xD1B54A32D192ED03)

static const char USO[] =
    "Uso: mpirun -np <procesos> permutado <cifrado.bin> \"<frase>\" [bits] [semilla]";

/*
 * Revuelve los bits de un entero. Es la rutina next de splitmix64.
 *
 * Entradas:
 *   valor  entero de partida
 * Salida:
 *   entero con los bits repartidos
 */
static uint64_t mezclar(uint64_t valor)
{
    uint64_t x = valor + GOLDEN;
    x = (x ^ (x >> 30)) * MEZCLA_1;
    x = (x ^ (x >> 27)) * MEZCLA_2;
    return x ^ (x >> 31);
}

/*
 * Convierte un texto a semilla.
 *
 * Entradas:
 *   texto    número decimal sin signo
 * Salidas:
 *   semilla  valor leído
 *   devuelve 1 si el texto es válido, 0 si no
 */
static int leer_semilla(const char *texto, uint64_t *semilla)
{
    if (texto[0] == '\0') {
        return 0;
    }
    for (const char *caracter = texto; *caracter != '\0'; caracter++) {
        if (!isdigit((unsigned char)*caracter)) {
            return 0;
        }
    }
    errno = 0;
    unsigned long long valor = strtoull(texto, NULL, 10);
    if (errno == ERANGE) {
        return 0;
    }
    *semilla = (uint64_t)valor;
    return 1;
}

int main(int argc, char **argv)
{
    Busqueda busqueda;

    if (!mpi_preparar_busqueda(&argc, &argv, USO, 1, &busqueda)) {
        return SALIDA_MPI;
    }

    /* argv es igual en todos los procesos, así que la semilla se lee sin repartirla. */
    uint64_t semilla = SEMILLA_POR_DEFECTO;
    if (argc == 5 && !leer_semilla(argv[4], &semilla)) {
        if (busqueda.id_proceso == 0) {
            fprintf(stderr, "Error: la semilla debe ser un entero sin signo\n");
        }
        busqueda_liberar(&busqueda);
        MPI_Finalize();
        return SALIDA_MPI;
    }

    /* A impar para que la biyección sea invertible módulo 2^bits. */
    uint64_t factor = mezclar(semilla) | UINT64_C(1);
    uint64_t desplazamiento = mezclar(semilla ^ DESPLAZA_C);
    uint64_t mascara = busqueda.total_llaves - 1;

    mpi_imprimir_datos(&busqueda, "permutado");
    if (busqueda.id_proceso == 0) {
        printf("Semilla: %" PRIu64 "\n", semilla);
        fflush(stdout);
    }

    uint64_t primer_indice = (uint64_t)busqueda.id_proceso;
    uint64_t salto = (uint64_t)busqueda.num_procesos;

    Aviso aviso;
    ResultadoLocal local = {0, 0, 0, 0.0};
    aviso_abrir(&aviso);
    double inicio = mpi_iniciar_cronometro();

    for (uint64_t indice = primer_indice; indice < busqueda.total_llaves; indice += salto) {
        /* El producto se envuelve módulo 2^64 y la máscara lo baja a 2^bits. */
        uint64_t candidata = ((factor * indice) + desplazamiento) & mascara;
        local.iteraciones++;
        if (des_probar_llave(candidata, busqueda.cifrado, busqueda.longitud, busqueda.frase,
                             busqueda.trabajo)) {
            local.encontrada = 1;
            local.llave = candidata;
            aviso_enviar(&busqueda, &aviso, candidata);
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
