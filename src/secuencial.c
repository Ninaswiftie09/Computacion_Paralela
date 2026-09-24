/*
 * secuencial.c
 * ------------
 * Versión SECUENCIAL del proyecto. Tres modos de uso:
 *
 *   ./secuencial cifrar   <archivo_entrada> <llave> <archivo_salida>
 *   ./secuencial descifrar <archivo_entrada> <llave> <archivo_salida>
 *   ./secuencial buscar   <archivo_cifrado> <palabra_clave> <llave_max>
 *
 * El modo "buscar" recorre secuencialmente (de 0 a llave_max) todo el
 * espacio de llaves probando cada una con tryKey, hasta encontrar la que
 * hace que el texto descifrado contenga "palabra_clave". Imprime la
 * llave encontrada y el tiempo de búsqueda (para el análisis de speedup
 * pedido en la Parte A.3 / Parte B).
 *
 * Compilar: ver Makefile (objetivo "secuencial").
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "des_lib.h"
#include "util.h"

static void uso(const char *prog) {
    fprintf(stderr,
        "Uso:\n"
        "  %s cifrar    <archivo_entrada> <llave> <archivo_salida>\n"
        "  %s descifrar <archivo_entrada> <llave> <archivo_salida>\n"
        "  %s buscar    <archivo_cifrado> <palabra_clave> <llave_max>\n",
        prog, prog, prog);
}

static int modo_cifrar(const char *in_path, unsigned long key,
                        const char *out_path, int cifrar) {
    size_t len, cap;
    unsigned char *buf = read_file_padded(in_path, &len, &cap);
    if (!buf) {
        fprintf(stderr, "Error: no se pudo leer '%s'\n", in_path);
        return 1;
    }

    size_t len_padded = pad_to_block(buf, len, cap);

    if (cifrar) {
        des_encrypt_buffer(key, buf, len_padded);
    } else {
        des_decrypt_buffer(key, buf, len_padded);
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "Error: no se pudo escribir '%s'\n", out_path);
        free(buf);
        return 1;
    }
    fwrite(buf, 1, len_padded, out);
    fclose(out);
    free(buf);

    printf("%s completado -> %s (%zu bytes, llave=%lu)\n",
           cifrar ? "Cifrado" : "Descifrado", out_path, len_padded, key);
    return 0;
}

static int modo_buscar(const char *ciph_path, const char *keyword,
                        unsigned long key_max) {
    size_t len, cap;
    unsigned char *buf = read_file_padded(ciph_path, &len, &cap);
    if (!buf) {
        fprintf(stderr, "Error: no se pudo leer '%s'\n", ciph_path);
        return 1;
    }

    printf("Buscando llave secuencialmente en [0, %lu] "
           "(%zu bytes cifrados, palabra clave: \"%s\")...\n",
           key_max, len, keyword);

    double t0 = wall_time_sec();

    unsigned long encontrada = (unsigned long)-1;
    unsigned long iteraciones = 0;
    char *descifrado = malloc(len + 1);

    for (unsigned long k = 0; k <= key_max; k++) {
        iteraciones++;
        if (try_key(k, buf, len, keyword, descifrado)) {
            encontrada = k;
            break;
        }
    }

    double t1 = wall_time_sec();

    if (encontrada != (unsigned long)-1) {
        printf("Llave encontrada: %lu\n", encontrada);
        printf("Texto descifrado: %s\n", descifrado);
    } else {
        printf("No se encontro la llave en el rango dado.\n");
    }
    printf("Iteraciones: %lu\n", iteraciones);
    printf("Tiempo de busqueda (secuencial): %.6f s\n", t1 - t0);

    free(descifrado);
    free(buf);
    return encontrada == (unsigned long)-1 ? 1 : 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        uso(argv[0]);
        return 1;
    }

    const char *modo = argv[1];

    if (strcmp(modo, "cifrar") == 0 || strcmp(modo, "descifrar") == 0) {
        if (argc != 5) { uso(argv[0]); return 1; }
        unsigned long key = strtoul(argv[3], NULL, 10);
        return modo_cifrar(argv[2], key, argv[4],
                            strcmp(modo, "cifrar") == 0);
    } else if (strcmp(modo, "buscar") == 0) {
        if (argc != 5) { uso(argv[0]); return 1; }
        unsigned long key_max = strtoul(argv[4], NULL, 10);
        return modo_buscar(argv[2], argv[3], key_max);
    } else {
        uso(argv[0]);
        return 1;
    }
}
