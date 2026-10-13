/**
 * des_tool.c - Cifra o descifra un archivo .txt con una llave DES de 56 bits.
 *
 * Uso:
 *   ./des_tool enc <entrada.txt> <salida.bin> <llave>
 *   ./des_tool dec <entrada.bin> <llave>        (imprime el texto en pantalla)
 */
#include "des_lib.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_key(const char *s, uint64_t *key) {
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno || end == s || *end != '\0' || v >= DES_KEYSPACE) return 0;
    *key = v;
    return 1;
}

/* Lee un archivo completo; retorna buffer (con relleno de ceros a multiplo de 8). */
static unsigned char *read_file(const char *path, size_t *n, size_t *padded) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "No se pudo abrir '%s': %s\n", path, strerror(errno)); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fprintf(stderr, "Archivo vacio: %s\n", path); fclose(f); return NULL; }
    *n = (size_t)sz;
    *padded = des_padded_len(*n);
    unsigned char *buf = calloc(*padded + 1, 1);
    if (!buf || fread(buf, 1, *n, f) != *n) { fprintf(stderr, "Error de lectura\n"); free(buf); fclose(f); return NULL; }
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    uint64_t key;
    size_t n, padded;
    if (argc == 5 && strcmp(argv[1], "enc") == 0) {
        if (!parse_key(argv[4], &key)) { fprintf(stderr, "Llave invalida (0 <= llave < 2^56)\n"); return 1; }
        unsigned char *buf = read_file(argv[2], &n, &padded);
        if (!buf) return 1;
        des_encrypt(key, buf, padded);
        FILE *o = fopen(argv[3], "wb");
        if (!o) { fprintf(stderr, "No se pudo crear '%s'\n", argv[3]); free(buf); return 1; }
        fwrite(buf, 1, padded, o);
        fclose(o);
        printf("Texto cifrado: %zu bytes -> '%s' (%zu bytes con relleno), llave=%llu\n",
               n, argv[3], padded, (unsigned long long)key);
        free(buf);
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "dec") == 0) {
        if (!parse_key(argv[3], &key)) { fprintf(stderr, "Llave invalida (0 <= llave < 2^56)\n"); return 1; }
        unsigned char *buf = read_file(argv[2], &n, &padded);
        if (!buf) return 1;
        if (n % DES_BLOCK) { fprintf(stderr, "El archivo cifrado debe ser multiplo de 8 bytes\n"); free(buf); return 1; }
        des_decrypt(key, buf, n);
        printf("Texto descifrado: \"%s\"\n", buf);
        free(buf);
        return 0;
    }
    fprintf(stderr, "Uso:\n  %s enc <entrada.txt> <salida.bin> <llave>\n  %s dec <entrada.bin> <llave>\n", argv[0], argv[0]);
    return 1;
}
