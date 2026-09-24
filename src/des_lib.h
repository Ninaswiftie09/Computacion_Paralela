/*
 * des_lib.h
 * ---------
 * Reemplazo de <rpc/des_crypt.h> (obsoleto en Linux moderno) usando la
 * librería OpenSSL (libcrypto). Expone las mismas 3 operaciones que pedía
 * el bruteforce.c original de la clase:
 *
 *   - encrypt(key, ciph, len): cifra "ciph" en el lugar (in-place) con la
 *     llave privada de 56 bits "key", en modo ECB, bloques de 8 bytes.
 *   - decrypt(key, ciph, len): operación inversa.
 *   - tryKey(key, ciph, len):  intenta descifrar con "key" y revisa si el
 *     resultado contiene la palabra clave de búsqueda (substring). No
 *     modifica el buffer original.
 *
 * Autor: (equipo)
 */
#ifndef DES_LIB_H
#define DES_LIB_H

#include <stddef.h>

/* Llave privada usada en todo el proyecto: valores de 0 a 2^56 - 1 */
#define DES_KEY_BITS 56
#define DES_MAX_KEY  ((1UL << DES_KEY_BITS) - 1)

/*
 * Convierte una llave de 56 bits (empaquetada en un long) a un bloque DES
 * de 8 bytes válido para OpenSSL, distribuyendo los 56 bits de entrada en
 * los 7 bits altos de cada uno de los 8 bytes (el bit bajo de cada byte
 * se deja para la paridad, que OpenSSL ignora si se usa
 * DES_set_key_unchecked).
 */
void long_to_des_key(unsigned long key, unsigned char out[8]);

/* Cifra "len" bytes de "buf" in-place usando la llave "key". Si "len" no
 * es múltiplo de 8, los bytes sobrantes del último bloque se dejan sin
 * tocar (se asume que el buffer ya viene alineado/rellenado por el
 * llamador, ver pad_buffer). */
void des_encrypt_buffer(unsigned long key, unsigned char *buf, size_t len);

/* Descifra "len" bytes de "buf" in-place usando la llave "key". */
void des_decrypt_buffer(unsigned long key, unsigned char *buf, size_t len);

/*
 * Intenta la llave "key": descifra una COPIA de "ciph" (len bytes) y
 * revisa (con strstr) si el texto resultante contiene "keyword".
 * Retorna 1 si la encontró (llave correcta), 0 si no.
 * "out" (opcional, puede ser NULL) recibe una copia del texto descifrado
 * si se encontró la llave correcta (debe tener espacio para len+1 bytes).
 */
int try_key(unsigned long key, const unsigned char *ciph, size_t len,
            const char *keyword, char *out);

/* Rellena "buf" (que tiene "len" bytes útiles, capacidad "cap") con ceros
 * hasta el siguiente múltiplo de 8. Retorna el nuevo largo (múltiplo de 8).
 * Requiere cap >= largo_redondeado. */
size_t pad_to_block(unsigned char *buf, size_t len, size_t cap);

#endif /* DES_LIB_H */
