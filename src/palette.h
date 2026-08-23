// palette.h -- Color de cada cuerpo a partir de su RAPIDEZ.
//
// No es color al azar: los cuerpos lentos (orbitas externas frias) salen azules
// y los rapidos (cayendo al nucleo, o lanzados en la colision) salen blancos y
// amarillos. Es la misma idea que el color de las estrellas por temperatura, y
// hace que la dinamica se lea de un vistazo.
#pragma once
#include <cstdint>

struct ColorRGB {
    uint8_t r, g, b;
};

// t en [0, 1]: 0 = lento (azul profundo), 1 = rapido (blanco caliente).
// Valores fuera de rango se recortan.
ColorRGB color_por_rapidez(float t);
