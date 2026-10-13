/*
 * cifrador.c
 * Cifra un archivo de texto con una llave de 56 bits, o descifra un archivo
 * cifrado y muestra el texto.
 *
 * Uso:
 *   cifrador cifrar <texto.txt> <salida.bin> <llave>
 *   cifrador descifrar <cifrado.bin> <llave>
 */
#include "comun.h"
#include "des.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bytes del cifrado que se muestran en pantalla como evidencia. */
#define MAX_BYTES_EN_PANTALLA 64

static const char USO[] =
    "Uso:\n"
    "  cifrador cifrar <texto.txt> <salida.bin> <llave>\n"
    "  cifrador descifrar <cifrado.bin> <llave>";

/*
 * Cifra un archivo de texto y guarda el resultado en otro archivo.
 *
 * Entradas:
 *   ruta_texto   archivo de texto plano
 *   ruta_salida  archivo cifrado que se crea
 *   llave        llave de 56 bits
 * Salida:
 *   código de salida del programa
 */
static int cifrar_archivo(const char *ruta_texto, const char *ruta_salida, uint64_t llave)
{
    size_t longitud = 0;
    unsigned char *datos = leer_archivo(ruta_texto, &longitud);
    if (datos == NULL) {
        return SALIDA_ERROR;
    }

    /* leer_archivo ya dejó en cero los bytes del relleno. */
    size_t longitud_rellena = des_longitud_rellena(longitud);
    des_cifrar(llave, datos, longitud_rellena);

    FILE *salida = fopen(ruta_salida, "wb");
    if (salida == NULL) {
        fprintf(stderr, "Error: no se pudo crear '%s': %s\n", ruta_salida, strerror(errno));
        free(datos);
        return SALIDA_ERROR;
    }
    size_t escritos = fwrite(datos, 1, longitud_rellena, salida);
    int cierre = fclose(salida);
    if (escritos != longitud_rellena || cierre != 0) {
        fprintf(stderr, "Error: no se pudo escribir '%s' completo\n", ruta_salida);
        free(datos);
        return SALIDA_ERROR;
    }

    size_t bytes_en_pantalla = longitud_rellena;
    if (bytes_en_pantalla > MAX_BYTES_EN_PANTALLA) {
        bytes_en_pantalla = MAX_BYTES_EN_PANTALLA;
    }
    printf("Archivo de texto: %s (%zu bytes)\n", ruta_texto, longitud);
    printf("Llave: %" PRIu64 "\n", llave);
    printf("Archivo cifrado: %s (%zu bytes)\n", ruta_salida, longitud_rellena);
    printf("Primeros %zu bytes del cifrado en hexadecimal:\n", bytes_en_pantalla);
    imprimir_hexadecimal(datos, bytes_en_pantalla);

    free(datos);
    return SALIDA_EXITO;
}

/*
 * Descifra un archivo y muestra el texto en pantalla.
 *
 * Entradas:
 *   ruta_cifrado  archivo cifrado
 *   llave         llave de 56 bits
 * Salida:
 *   código de salida del programa
 */
static int descifrar_archivo(const char *ruta_cifrado, uint64_t llave)
{
    size_t longitud = 0;
    unsigned char *datos = leer_archivo(ruta_cifrado, &longitud);
    if (datos == NULL) {
        return SALIDA_ERROR;
    }
    if (longitud % DES_TAM_BLOQUE != 0) {
        fprintf(stderr, "Error: '%s' no es un archivo cifrado, su tamaño no es múltiplo de %d\n",
                ruta_cifrado, DES_TAM_BLOQUE);
        free(datos);
        return SALIDA_ERROR;
    }

    des_descifrar(llave, datos, longitud);

    printf("Archivo cifrado: %s (%zu bytes)\n", ruta_cifrado, longitud);
    printf("Llave: %" PRIu64 "\n", llave);
    printf("Texto descifrado: ");
    imprimir_texto(datos);

    free(datos);
    return SALIDA_EXITO;
}

int main(int argc, char **argv)
{
    int pide_cifrar = argc == 5 && strcmp(argv[1], "cifrar") == 0;
    int pide_descifrar = argc == 4 && strcmp(argv[1], "descifrar") == 0;

    if (!pide_cifrar && !pide_descifrar) {
        fprintf(stderr, "%s\n", USO);
        return SALIDA_ERROR;
    }

    uint64_t llave = 0;
    const char *texto_llave = argv[argc - 1];
    if (!leer_llave(texto_llave, &llave)) {
        fprintf(stderr, "Error: la llave debe ser un entero entre 0 y 2^%d - 1\n", DES_BITS_LLAVE);
        return SALIDA_ERROR;
    }

    if (pide_cifrar) {
        return cifrar_archivo(argv[2], argv[3], llave);
    }
    return descifrar_archivo(argv[2], llave);
}
