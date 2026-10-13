/**
 * des_lib.c - Implementacion de la interfaz DES sobre OpenSSL (DES_ecb_encrypt).
 */
#define OPENSSL_SUPPRESS_DEPRECATED
#include "des_lib.h"
#include <openssl/des.h>
#include <string.h>

size_t des_padded_len(size_t n) {
    return (n + DES_BLOCK - 1) / DES_BLOCK * DES_BLOCK;
}

/**
 * Expande la llave de 56 bits a los 8 bytes (64 bits) que exige DES:
 * cada byte lleva 7 bits de llave en sus bits altos y 1 bit de paridad
 * impar en el bit menos significativo.
 */
static void expand_key(uint64_t key, DES_cblock *out) {
    for (int i = 0; i < 8; i++) {
        unsigned char b = (unsigned char)(((key >> (7 * i)) & 0x7F) << 1);
        (*out)[i] = b;
    }
    DES_set_odd_parity(out);
}

static void run(uint64_t key, unsigned char *buf, size_t len, int mode) {
    DES_cblock kb;
    DES_key_schedule ks;
    expand_key(key, &kb);
    DES_set_key_unchecked(&kb, &ks);   /* sin chequeo de llaves debiles */
    for (size_t off = 0; off + DES_BLOCK <= len; off += DES_BLOCK) {
        DES_cblock *blk = (DES_cblock *)(buf + off);
        DES_ecb_encrypt(blk, blk, &ks, mode);
    }
}

void des_encrypt(uint64_t key, unsigned char *buf, size_t len) {
    run(key, buf, len, DES_ENCRYPT);
}

void des_decrypt(uint64_t key, unsigned char *buf, size_t len) {
    run(key, buf, len, DES_DECRYPT);
}

int des_try_key(uint64_t key, const unsigned char *cipher, size_t len,
                const char *keyword, unsigned char *scratch) {
    memcpy(scratch, cipher, len);
    des_decrypt(key, scratch, len);
    scratch[len] = '\0';
    return strstr((const char *)scratch, keyword) != NULL;
}
