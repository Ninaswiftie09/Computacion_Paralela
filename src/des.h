/*
 * des.h
 * Cifrado DES en modo ECB con llaves de 56 bits.
 *
 * Usa OpenSSL en lugar de <rpc/des_crypt.h>, que ya no viene con glibc.
 * Equivalencias con las rutinas de bruteforce.c:
 *   encrypt  es  des_cifrar
 *   decrypt  es  des_descifrar
 *   tryKey   es  des_probar_llave
 *
 * Convenciones:
 *   - La llave es un entero sin signo menor que 2^56.
 *   - Los datos se transforman en su mismo arreglo, en bloques de 8 bytes.
 *   - El texto plano se rellena con ceros hasta completar el último bloque.
 */
#ifndef DES_H
#define DES_H

#include <stddef.h>
#include <stdint.h>

#define DES_BITS_LLAVE 56
#define DES_TAM_BLOQUE 8
#define DES_ESPACIO_LLAVES (UINT64_C(1) << DES_BITS_LLAVE)

/*
 * Redondea una longitud al siguiente múltiplo de 8.
 *
 * Entradas:
 *   longitud  cantidad de bytes del texto
 * Salida:
 *   cantidad de bytes que ocupa el texto con relleno
 */
size_t des_longitud_rellena(size_t longitud);

/*
 * Cifra un arreglo con una llave de 56 bits.
 *
 * Entradas:
 *   llave     llave de 56 bits
 *   datos     texto plano con relleno
 *   longitud  cantidad de bytes, múltiplo de 8
 * Salida:
 *   datos queda sobrescrito con el texto cifrado
 */
void des_cifrar(uint64_t llave, unsigned char *datos, size_t longitud);

/*
 * Descifra un arreglo con una llave de 56 bits.
 *
 * Entradas:
 *   llave     llave de 56 bits
 *   datos     texto cifrado
 *   longitud  cantidad de bytes, múltiplo de 8
 * Salida:
 *   datos queda sobrescrito con el texto descifrado
 */
void des_descifrar(uint64_t llave, unsigned char *datos, size_t longitud);

/*
 * Prueba una llave candidata sin modificar el texto cifrado.
 *
 * Entradas:
 *   llave     llave de 56 bits a probar
 *   cifrado   texto cifrado
 *   longitud  cantidad de bytes del cifrado, múltiplo de 8
 *   frase     frase clave que debe aparecer en el texto plano
 *   trabajo   arreglo de al menos longitud + 1 bytes para el descifrado
 * Salida:
 *   1 si el texto descifrado contiene la frase, 0 si no
 */
int des_probar_llave(uint64_t llave, const unsigned char *cifrado, size_t longitud,
                     const char *frase, unsigned char *trabajo);

#endif
