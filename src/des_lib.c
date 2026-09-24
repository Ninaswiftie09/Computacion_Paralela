/*
 * des_lib.c
 * ---------
 * Implementación de encrypt/decrypt/tryKey usando la API "legacy" de
 * OpenSSL para DES (DES_set_key_unchecked + DES_ecb_encrypt), en modo
 * ECB y bloques de 8 bytes, tal como lo hacía el bruteforce.c original
 * con <rpc/des_crypt.h>.
 *
 * Nota de compilación: OpenSSL 3.x marca las funciones DES_* como
 * "deprecated" (DES ya no se considera seguro para uso real), pero las
 * sigue exponiendo. Por eso compilamos con
 * -Wno-deprecated-declarations (ver Makefile). Para este proyecto DES se
 * usa únicamente como ejercicio académico de fuerza bruta, no como
 * mecanismo de seguridad real.
 */
#define _GNU_SOURCE
#include <string.h>
#include <openssl/des.h>
#include "des_lib.h"

void long_to_des_key(unsigned long key, unsigned char out[8]) {
    /* Tomamos los 56 bits de "key" en grupos de 7 bits y los colocamos
     * en los 7 bits altos de cada byte de salida (bit de paridad en 0,
     * DES_set_key_unchecked no valida ni corrige paridad). */
    int i;
    for (i = 0; i < 8; i++) {
        unsigned long bits7 = (key >> (7 * (7 - i))) & 0x7F; /* 7 bits */
        out[i] = (unsigned char)(bits7 << 1);
    }
}

void des_encrypt_buffer(unsigned long key, unsigned char *buf, size_t len) {
    DES_cblock des_key;
    DES_key_schedule schedule;
    size_t i;

    long_to_des_key(key, des_key);
    DES_set_key_unchecked(&des_key, &schedule);

    for (i = 0; i + 8 <= len; i += 8) {
        DES_ecb_encrypt((DES_cblock *)(buf + i), (DES_cblock *)(buf + i),
                         &schedule, DES_ENCRYPT);
    }
}

void des_decrypt_buffer(unsigned long key, unsigned char *buf, size_t len) {
    DES_cblock des_key;
    DES_key_schedule schedule;
    size_t i;

    long_to_des_key(key, des_key);
    DES_set_key_unchecked(&des_key, &schedule);

    for (i = 0; i + 8 <= len; i += 8) {
        DES_ecb_encrypt((DES_cblock *)(buf + i), (DES_cblock *)(buf + i),
                         &schedule, DES_DECRYPT);
    }
}

int try_key(unsigned long key, const unsigned char *ciph, size_t len,
            const char *keyword, char *out) {
    /* Buffer local de trabajo (no tocamos el original "ciph") */
    unsigned char tmp[4096];
    if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;

    memcpy(tmp, ciph, len);
    des_decrypt_buffer(key, tmp, len);
    tmp[len] = '\0';

    if (strstr((const char *)tmp, keyword) != NULL) {
        if (out != NULL) {
            memcpy(out, tmp, len + 1);
        }
        return 1;
    }
    return 0;
}

size_t pad_to_block(unsigned char *buf, size_t len, size_t cap) {
    size_t padded = ((len + 7) / 8) * 8;
    if (padded == 0) padded = 8; /* siempre al menos un bloque */
    if (padded > cap) padded = (cap / 8) * 8;
    if (padded > len) {
        memset(buf + len, 0, padded - len);
    }
    return padded;
}
