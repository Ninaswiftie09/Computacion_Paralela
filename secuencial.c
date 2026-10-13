/**
 * secuencial.c - Busqueda de llave DES por fuerza bruta, version secuencial.
 *
 * Uso: ./secuencial <cifrado.bin> "<frase clave>" [bits_espacio]
 *   bits_espacio: se prueban las llaves 0 .. 2^bits_espacio - 1 (por defecto 56).
 *   Sirve para medir tiempos con distintas "longitudes de llave".
 * Salida: llave, iteraciones, tiempo y llaves/segundo.
 */
#include "des_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Uso: %s <cifrado.bin> \"<frase>\" [bits_espacio<=56]\n", argv[0]);
        return 1;
    }
    int bits = (argc == 4) ? atoi(argv[3]) : DES_KEY_BITS;
    if (bits < 1 || bits > DES_KEY_BITS) { fprintf(stderr, "bits_espacio debe estar en [1,56]\n"); return 1; }
    const char *kw = argv[2];
    if (strlen(kw) < 4) fprintf(stderr, "Aviso: frase muy corta, riesgo de falsos positivos\n");

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    if (len <= 0 || len % DES_BLOCK) { fprintf(stderr, "Archivo cifrado invalido\n"); fclose(f); return 1; }
    unsigned char *cipher = calloc(len + 1, 1), *scratch = malloc(len + 1);
    if (!cipher || !scratch || fread(cipher, 1, len, f) != (size_t)len) { fprintf(stderr, "Error de lectura/memoria\n"); return 1; }
    fclose(f);

    uint64_t limit = ((uint64_t)1) << bits;
    uint64_t found = 0, iters = 0;
    int ok = 0;
    double t0 = now();
    for (uint64_t k = 0; k < limit; k++) {
        iters++;
        if (des_try_key(k, cipher, len, kw, scratch)) { found = k; ok = 1; break; }
    }
    double dt = now() - t0;

    if (ok) {
        des_decrypt(found, cipher, len);
        printf("Archivo: %s | frase: \"%s\"\n", argv[1], kw);
        printf("Llave encontrada: %llu\nTexto: \"%s\"\n", (unsigned long long)found, cipher);
    } else {
        printf("Llave NO encontrada en el espacio de 2^%d llaves\n", bits);
    }
    printf("Iteraciones: %llu | Tiempo: %.4f s | %.2f Mllaves/s\n",
           (unsigned long long)iters, dt, dt > 0 ? iters / dt / 1e6 : 0.0);
    free(cipher); free(scratch);
    return ok ? 0 : 2;
}
