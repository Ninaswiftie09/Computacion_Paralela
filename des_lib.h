/**
 * des_lib.h - Interfaz comun de cifrado DES para el Proyecto 2 (UVG, CC3069).
 *
 * Reemplaza a <rpc/des_crypt.h> usando la implementacion DES de OpenSSL.
 * Convenciones (acordar con todo el equipo):
 *   - La llave es un entero de 56 bits sin signo (uint64_t), 0 <= key < 2^56.
 *   - Los buffers se cifran/descifran IN PLACE en bloques ECB de 8 bytes,
 *     por lo que 'len' debe ser multiplo de 8.
 *   - El texto plano se rellena con ceros ('\0') hasta completar un bloque.
 *   - Un intento de llave es exitoso si el descifrado contiene la frase clave.
 */
#ifndef DES_LIB_H
#define DES_LIB_H

#include <stddef.h>
#include <stdint.h>

#define DES_KEY_BITS 56
#define DES_BLOCK 8

/** Espacio total de llaves: 2^56. */
#define DES_KEYSPACE (((uint64_t)1) << DES_KEY_BITS)

/** Redondea n hacia arriba al siguiente multiplo de 8. */
size_t des_padded_len(size_t n);

/** Cifra 'len' bytes de 'buf' in place con la llave de 56 bits 'key'. */
void des_encrypt(uint64_t key, unsigned char *buf, size_t len);

/** Descifra 'len' bytes de 'buf' in place con la llave de 56 bits 'key'. */
void des_decrypt(uint64_t key, unsigned char *buf, size_t len);

/**
 * Prueba una llave: copia 'cipher' a 'scratch', descifra y busca 'keyword'.
 * Retorna 1 si 'keyword' aparece en el texto descifrado, 0 en caso contrario.
 * 'scratch' debe tener al menos len+1 bytes (se le agrega '\0' final).
 */
int des_try_key(uint64_t key, const unsigned char *cipher, size_t len,
                const char *keyword, unsigned char *scratch);

#endif
