// render.h -- Dibujo del sistema sobre un framebuffer en memoria.
//
// El framebuffer es un vector de uint32_t en formato ARGB8888 (0xAARRGGBB), que
// es exactamente lo que espera SDL_UpdateTexture con SDL_PIXELFORMAT_ARGB8888.
// Empaquetar el pixel entero evita depender del orden de bytes de la maquina.
#pragma once
#include <cstdint>
#include <vector>
#include "nbody.h"

// Atenua todo el framebuffer multiplicando cada canal por factor/256.
// En vez de borrar la pantalla, la desvanecemos: lo que queda son las ESTELAS
// de las orbitas, que es lo que hace legible el movimiento.
void desvanecer(std::vector<uint32_t>& fb, uint32_t factor);

// Pinta el fondo de un solo color (usado cuando las estelas estan apagadas).
void limpiar(std::vector<uint32_t>& fb, uint32_t color);

// Dibuja cada cuerpo como un pequeno destello aditivo, coloreado por su rapidez.
// El brillo se SUMA, asi que donde se acumulan muchos cuerpos se satura a blanco
// y se ve el nucleo de la galaxia.
void dibujar_cuerpos(std::vector<uint32_t>& fb, int ancho, int alto,
                     const SistemaNCuerpos& sistema);
