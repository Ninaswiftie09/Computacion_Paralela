/*
 * comun.c
 * Implementación de comun.h.
 */
#define _POSIX_C_SOURCE 200809L
#include "comun.h"

#include "des.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define BYTES_POR_FILA 16

/*
 * Revisa que un texto tenga solo dígitos decimales. strtoull acepta signos
 * y espacios, y eso dejaría pasar valores como "-1".
 *
 * Entradas:
 *   texto  cadena a revisar
 * Salida:
 *   1 si todos los caracteres son dígitos, 0 si no o si está vacía
 */
static int es_numero_sin_signo(const char *texto)
{
    if (texto[0] == '\0') {
        return 0;
    }
    for (const char *caracter = texto; *caracter != '\0'; caracter++) {
        if (!isdigit((unsigned char)*caracter)) {
            return 0;
        }
    }
    return 1;
}

/*
 * Revisa que la frase clave sirva para reconocer el texto plano.
 *
 * Entradas:
 *   frase             frase clave
 *   longitud_cifrado  bytes del texto cifrado
 * Salida:
 *   1 si la frase es válida, 0 si no
 */
static int validar_frase(const char *frase, size_t longitud_cifrado)
{
    size_t largo = strlen(frase);

    if (largo == 0) {
        fprintf(stderr, "Error: la frase clave está vacía\n");
        return 0;
    }
    if (largo > longitud_cifrado) {
        fprintf(stderr, "Error: la frase clave es más larga que el texto cifrado\n");
        return 0;
    }
    if (largo < MIN_LARGO_FRASE) {
        fprintf(stderr,
                "Aviso: una frase de menos de %d caracteres puede aparecer con una llave incorrecta\n",
                MIN_LARGO_FRASE);
    }
    return 1;
}

int leer_llave(const char *texto, uint64_t *llave)
{
    if (!es_numero_sin_signo(texto)) {
        return 0;
    }
    errno = 0;
    unsigned long long valor = strtoull(texto, NULL, 10);
    if (errno == ERANGE || valor >= DES_ESPACIO_LLAVES) {
        return 0;
    }
    *llave = (uint64_t)valor;
    return 1;
}

int leer_bits(const char *texto, int *bits)
{
    if (!es_numero_sin_signo(texto)) {
        return 0;
    }
    errno = 0;
    unsigned long valor = strtoul(texto, NULL, 10);
    if (errno == ERANGE || valor < 1 || valor > DES_BITS_LLAVE) {
        return 0;
    }
    *bits = (int)valor;
    return 1;
}

unsigned char *leer_archivo(const char *ruta, size_t *longitud)
{
    struct stat datos_archivo;

    if (stat(ruta, &datos_archivo) != 0) {
        fprintf(stderr, "Error: no se pudo abrir '%s': %s\n", ruta, strerror(errno));
        return NULL;
    }
    if (!S_ISREG(datos_archivo.st_mode)) {
        fprintf(stderr, "Error: '%s' no es un archivo\n", ruta);
        return NULL;
    }
    if (datos_archivo.st_size == 0) {
        fprintf(stderr, "Error: el archivo '%s' está vacío\n", ruta);
        return NULL;
    }
    if (datos_archivo.st_size > MAX_BYTES_ARCHIVO) {
        fprintf(stderr, "Error: el archivo '%s' supera los %d bytes permitidos\n", ruta,
                MAX_BYTES_ARCHIVO);
        return NULL;
    }

    size_t bytes = (size_t)datos_archivo.st_size;
    unsigned char *contenido = calloc(des_longitud_rellena(bytes) + 1, 1);
    if (contenido == NULL) {
        fprintf(stderr, "Error: no hay memoria para cargar '%s'\n", ruta);
        return NULL;
    }

    FILE *archivo = fopen(ruta, "rb");
    if (archivo == NULL) {
        fprintf(stderr, "Error: no se pudo abrir '%s': %s\n", ruta, strerror(errno));
        free(contenido);
        return NULL;
    }
    size_t leidos = fread(contenido, 1, bytes, archivo);
    fclose(archivo);
    if (leidos != bytes) {
        fprintf(stderr, "Error: no se pudo leer '%s' completo\n", ruta);
        free(contenido);
        return NULL;
    }

    *longitud = bytes;
    return contenido;
}

int busqueda_preparar(const char *ruta_cifrado, const char *frase, const char *texto_bits,
                      Busqueda *busqueda)
{
    memset(busqueda, 0, sizeof *busqueda);
    busqueda->ruta_cifrado = ruta_cifrado;
    busqueda->bits_espacio = DES_BITS_LLAVE;
    busqueda->num_procesos = 1;

    if (texto_bits != NULL && !leer_bits(texto_bits, &busqueda->bits_espacio)) {
        fprintf(stderr, "Error: bits debe ser un entero entre 1 y %d\n", DES_BITS_LLAVE);
        return 0;
    }
    busqueda->total_llaves = UINT64_C(1) << busqueda->bits_espacio;

    busqueda->cifrado = leer_archivo(ruta_cifrado, &busqueda->longitud);
    if (busqueda->cifrado == NULL) {
        return 0;
    }
    if (busqueda->longitud % DES_TAM_BLOQUE != 0) {
        fprintf(stderr, "Error: '%s' no es un archivo cifrado, su tamaño no es múltiplo de %d\n",
                ruta_cifrado, DES_TAM_BLOQUE);
        busqueda_liberar(busqueda);
        return 0;
    }
    if (!validar_frase(frase, busqueda->longitud)) {
        busqueda_liberar(busqueda);
        return 0;
    }

    size_t bytes_frase = strlen(frase) + 1;
    busqueda->frase = malloc(bytes_frase);
    busqueda->trabajo = malloc(busqueda->longitud + 1);
    if (busqueda->frase == NULL || busqueda->trabajo == NULL) {
        fprintf(stderr, "Error: no hay memoria para preparar la búsqueda\n");
        busqueda_liberar(busqueda);
        return 0;
    }
    memcpy(busqueda->frase, frase, bytes_frase);
    return 1;
}

void busqueda_liberar(Busqueda *busqueda)
{
    free(busqueda->frase);
    free(busqueda->cifrado);
    free(busqueda->trabajo);
    busqueda->frase = NULL;
    busqueda->cifrado = NULL;
    busqueda->trabajo = NULL;
}

void imprimir_datos_busqueda(const Busqueda *busqueda, const char *programa)
{
    printf("Programa: %s\n", programa);
    printf("Procesos: %d\n", busqueda->num_procesos);
    printf("Archivo cifrado: %s\n", busqueda->ruta_cifrado);
    printf("Frase clave: \"%s\"\n", busqueda->frase);
    printf("Espacio de llaves: 2^%d (%" PRIu64 " llaves)\n", busqueda->bits_espacio,
           busqueda->total_llaves);
    /* Se vacía la salida para que estos datos se vean mientras dura la búsqueda. */
    fflush(stdout);
}

void imprimir_llave(const Busqueda *busqueda, int encontrada, uint64_t llave)
{
    if (!encontrada) {
        printf("Llave no encontrada entre las primeras 2^%d llaves\n", busqueda->bits_espacio);
        return;
    }

    memcpy(busqueda->trabajo, busqueda->cifrado, busqueda->longitud);
    des_descifrar(llave, busqueda->trabajo, busqueda->longitud);
    busqueda->trabajo[busqueda->longitud] = '\0';

    printf("Llave encontrada: %" PRIu64 "\n", llave);
    printf("Texto descifrado: ");
    imprimir_texto(busqueda->trabajo);
}

void imprimir_tiempo(double segundos, uint64_t iteraciones)
{
    printf("Tiempo: %.6f s\n", segundos);
    if (segundos > 0.0) {
        printf("Velocidad: %.2f millones de llaves por segundo\n",
               (double)iteraciones / segundos / 1e6);
    }
}

void imprimir_texto(const unsigned char *texto)
{
    int ultimo = '\n';

    for (const unsigned char *caracter = texto; *caracter != '\0'; caracter++) {
        int es_control = iscntrl(*caracter) && *caracter != '\n' && *caracter != '\t';
        ultimo = es_control ? '.' : *caracter;
        putchar(ultimo);
    }
    if (ultimo != '\n') {
        putchar('\n');
    }
}

void imprimir_hexadecimal(const unsigned char *datos, size_t longitud)
{
    for (size_t posicion = 0; posicion < longitud; posicion++) {
        int fin_de_fila = (posicion + 1) % BYTES_POR_FILA == 0 || posicion + 1 == longitud;
        printf("%02x%c", datos[posicion], fin_de_fila ? '\n' : ' ');
    }
}

double segundos_ahora(void)
{
    struct timespec instante;

    clock_gettime(CLOCK_MONOTONIC, &instante);
    return (double)instante.tv_sec + (double)instante.tv_nsec * 1e-9;
}
