/*
 * comun.h
 * Rutinas que comparten todos los programas: lectura de argumentos,
 * carga de archivos, impresión de resultados y cronómetro.
 */
#ifndef COMUN_H
#define COMUN_H

#include <stddef.h>
#include <stdint.h>

/* Códigos de salida de los programas. */
#define SALIDA_EXITO 0
#define SALIDA_ERROR 1
#define SALIDA_NO_ENCONTRADA 2

/* Tamaño máximo de un archivo de texto o cifrado. */
#define MAX_BYTES_ARCHIVO (1024 * 1024)

/* Con menos caracteres la frase puede aparecer por azar con una llave incorrecta. */
#define MIN_LARGO_FRASE 4

/* Datos de una búsqueda de llave. */
typedef struct {
    const char *ruta_cifrado; /* nombre del archivo cifrado */
    char *frase;              /* frase clave que debe aparecer en el texto plano */
    int bits_espacio;         /* se prueban las llaves menores que 2^bits_espacio */
    uint64_t total_llaves;    /* 2^bits_espacio */
    unsigned char *cifrado;   /* texto cifrado, longitud + 1 bytes */
    unsigned char *trabajo;   /* arreglo para descifrar, longitud + 1 bytes */
    size_t longitud;          /* bytes del cifrado, múltiplo de 8 */
    int id_proceso;           /* 0 en la versión secuencial */
    int num_procesos;         /* 1 en la versión secuencial */
} Busqueda;

/*
 * Convierte un texto a llave.
 *
 * Entradas:
 *   texto  número decimal sin signo
 * Salidas:
 *   llave  valor leído, menor que 2^56
 *   devuelve 1 si el texto es una llave válida, 0 si no
 */
int leer_llave(const char *texto, uint64_t *llave);

/*
 * Convierte un texto a cantidad de bits del espacio de llaves.
 *
 * Entradas:
 *   texto  número decimal sin signo
 * Salidas:
 *   bits   valor leído, entre 1 y 56
 *   devuelve 1 si el texto es válido, 0 si no
 */
int leer_bits(const char *texto, int *bits);

/*
 * Carga un archivo completo en memoria. Si falla, explica el motivo en stderr.
 *
 * Entradas:
 *   ruta      ubicación del archivo
 * Salidas:
 *   longitud  cantidad de bytes leídos
 *   devuelve el contenido, o NULL si hubo un error. El arreglo tiene espacio
 *   para el relleno a múltiplo de 8 más un cero final. Se libera con free.
 */
unsigned char *leer_archivo(const char *ruta, size_t *longitud);

/*
 * Valida los argumentos de una búsqueda y carga el archivo cifrado.
 * Si algo falla, explica el motivo en stderr.
 *
 * Entradas:
 *   ruta_cifrado  ubicación del archivo cifrado
 *   frase         frase clave
 *   texto_bits    bits del espacio de llaves, o NULL para usar 56
 * Salidas:
 *   busqueda      datos listos para buscar
 *   devuelve 1 si todo es válido, 0 si no
 */
int busqueda_preparar(const char *ruta_cifrado, const char *frase, const char *texto_bits,
                      Busqueda *busqueda);

/*
 * Libera la memoria de una búsqueda.
 *
 * Entradas:
 *   busqueda  búsqueda que ya no se va a usar
 */
void busqueda_liberar(Busqueda *busqueda);

/*
 * Imprime el programa, los procesos, el archivo cifrado, la frase y el espacio.
 *
 * Entradas:
 *   busqueda  búsqueda preparada
 *   programa  nombre del programa
 */
void imprimir_datos_busqueda(const Busqueda *busqueda, const char *programa);

/*
 * Imprime la llave y el texto descifrado, o avisa que no se encontró.
 *
 * Entradas:
 *   busqueda    búsqueda preparada
 *   encontrada  1 si hay llave, 0 si no
 *   llave       llave encontrada
 */
void imprimir_llave(const Busqueda *busqueda, int encontrada, uint64_t llave);

/*
 * Imprime el tiempo de búsqueda y la velocidad en llaves por segundo.
 *
 * Entradas:
 *   segundos     tiempo de la búsqueda
 *   iteraciones  llaves probadas en total
 */
void imprimir_tiempo(double segundos, uint64_t iteraciones);

/*
 * Imprime un texto hasta su primer cero. Sustituye los caracteres de
 * control por puntos para no alterar la terminal.
 *
 * Entradas:
 *   texto  cadena terminada en cero
 */
void imprimir_texto(const unsigned char *texto);

/*
 * Imprime bytes en hexadecimal.
 *
 * Entradas:
 *   datos     bytes a mostrar
 *   longitud  cantidad de bytes
 */
void imprimir_hexadecimal(const unsigned char *datos, size_t longitud);

/*
 * Lee un reloj que no retrocede.
 *
 * Salida:
 *   segundos transcurridos desde un punto fijo
 */
double segundos_ahora(void);

#endif
