// palette.h
// -----------------------------------------------------------------------------
// Mapea una temperatura (0..1, normalizada) a un color RGB siguiendo una
// rampa de "cuerpo negro": blanco -> amarillo -> naranja -> rojo -> gris humo.
// No es un color aleatorio puro; el enunciado pide colores pseudoaleatorios,
// y aqui la variedad sale del jitter por particula sobre la temperatura
// (ver particles.cpp), no de tirar un color al azar sin sentido fisico.
// -----------------------------------------------------------------------------
#pragma once
#include <cstdint>

struct ColorRGB {
    uint8_t r, g, b;
};

// t en [0, 1]: 1 = recien nacida / muy caliente (blanco), 0 = fria (humo gris)
ColorRGB color_por_temperatura(float t);
