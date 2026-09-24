#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* Lee un archivo completo en memoria. Retorna un buffer con malloc (el
 * llamador debe hacer free) y escribe el tamaño leído en *out_len.
 * El buffer tiene capacidad out_len + 4096 bytes extra (para poder
 * rellenar/paddear sin reservar de nuevo). Retorna NULL si falla. */
unsigned char *read_file_padded(const char *path, size_t *out_len,
                                 size_t *out_cap);

/* Tiempo actual en segundos (double), reloj monotónico de alta
 * resolución. Válido tanto en el programa secuencial como en cada
 * proceso MPI (para MPI se prefiere MPI_Wtime, pero esta función sirve
 * de respaldo / para el programa secuencial). */
double wall_time_sec(void);

#endif /* UTIL_H */
