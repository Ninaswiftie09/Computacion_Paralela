/*
 * des.c
 * Implementación de des.h sobre la interfaz DES de OpenSSL.
 */
#define OPENSSL_SUPPRESS_DEPRECATED
#include "des.h"

#include <openssl/des.h>
#include <string.h>

#define BITS_DE_LLAVE_POR_BYTE 7
#define MASCARA_SIETE_BITS 0x7F

/*
 * Reparte los 56 bits de la llave en 8 bytes, 7 bits por byte.
 * El bit menos significativo de cada byte queda para la paridad impar
 * que pide DES. Es el mismo reparto que hace bruteforce.c.
 *
 * Entradas:
 *   llave      llave de 56 bits
 * Salida:
 *   llave_des  los 8 bytes que recibe OpenSSL
 */
static void expandir_llave(uint64_t llave, DES_cblock *llave_des)
{
    for (int byte = 0; byte < DES_TAM_BLOQUE; byte++) {
        uint64_t grupo = (llave >> (BITS_DE_LLAVE_POR_BYTE * byte)) & MASCARA_SIETE_BITS;
        (*llave_des)[byte] = (unsigned char)(grupo << 1);
    }
    DES_set_odd_parity(llave_des);
}

/*
 * Cifra o descifra un arreglo bloque por bloque.
 *
 * Entradas:
 *   llave      llave de 56 bits
 *   datos      arreglo a transformar
 *   longitud   cantidad de bytes, múltiplo de 8
 *   operacion  DES_ENCRYPT o DES_DECRYPT
 * Salida:
 *   datos queda sobrescrito con el resultado
 */
static void transformar(uint64_t llave, unsigned char *datos, size_t longitud, int operacion)
{
    DES_cblock llave_des;
    DES_key_schedule subllaves;

    expandir_llave(llave, &llave_des);
    /* Sin verificación, para no descartar las llaves débiles del espacio. */
    DES_set_key_unchecked(&llave_des, &subllaves);

    for (size_t inicio = 0; inicio + DES_TAM_BLOQUE <= longitud; inicio += DES_TAM_BLOQUE) {
        DES_cblock *bloque = (DES_cblock *)(datos + inicio);
        DES_ecb_encrypt(bloque, bloque, &subllaves, operacion);
    }
}

size_t des_longitud_rellena(size_t longitud)
{
    return (longitud + DES_TAM_BLOQUE - 1) / DES_TAM_BLOQUE * DES_TAM_BLOQUE;
}

void des_cifrar(uint64_t llave, unsigned char *datos, size_t longitud)
{
    transformar(llave, datos, longitud, DES_ENCRYPT);
}

void des_descifrar(uint64_t llave, unsigned char *datos, size_t longitud)
{
    transformar(llave, datos, longitud, DES_DECRYPT);
}

int des_probar_llave(uint64_t llave, const unsigned char *cifrado, size_t longitud,
                     const char *frase, unsigned char *trabajo)
{
    /* El descifrado sobrescribe sus datos, por eso se trabaja sobre una copia. */
    memcpy(trabajo, cifrado, longitud);
    des_descifrar(llave, trabajo, longitud);
    /* strstr necesita una cadena terminada en cero. */
    trabajo[longitud] = '\0';
    return strstr((const char *)trabajo, frase) != NULL;
}
