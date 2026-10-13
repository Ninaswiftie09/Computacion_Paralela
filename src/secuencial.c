/*
 * secuencial.c
 * Búsqueda de la llave por fuerza bruta en un solo proceso. Prueba las
 * llaves 0, 1, 2 y siguientes hasta que el texto descifrado contiene la
 * frase clave.
 *
 * Uso: secuencial <cifrado.bin> "<frase>" [bits]
 *   bits  se prueban las llaves menores que 2^bits. Por omisión, 56.
 */
#include "comun.h"
#include "des.h"

#include <inttypes.h>
#include <stdio.h>

static const char USO[] = "Uso: secuencial <cifrado.bin> \"<frase>\" [bits]";

int main(int argc, char **argv)
{
    Busqueda busqueda;

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "%s\n", USO);
        return SALIDA_ERROR;
    }
    const char *texto_bits = argc == 4 ? argv[3] : NULL;
    if (!busqueda_preparar(argv[1], argv[2], texto_bits, &busqueda)) {
        return SALIDA_ERROR;
    }
    imprimir_datos_busqueda(&busqueda, "secuencial");

    int encontrada = 0;
    uint64_t llave_encontrada = 0;
    uint64_t iteraciones = 0;
    double inicio = segundos_ahora();

    for (uint64_t llave = 0; llave < busqueda.total_llaves; llave++) {
        iteraciones++;
        if (des_probar_llave(llave, busqueda.cifrado, busqueda.longitud, busqueda.frase,
                             busqueda.trabajo)) {
            encontrada = 1;
            llave_encontrada = llave;
            break;
        }
    }
    double segundos = segundos_ahora() - inicio;

    imprimir_llave(&busqueda, encontrada, llave_encontrada);
    printf("Iteraciones: %" PRIu64 "\n", iteraciones);
    imprimir_tiempo(segundos, iteraciones);

    busqueda_liberar(&busqueda);
    return encontrada ? SALIDA_EXITO : SALIDA_NO_ENCONTRADA;
}
