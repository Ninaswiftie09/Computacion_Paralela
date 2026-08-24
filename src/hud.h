// hud.h -- Panel de metricas dibujado sobre el framebuffer.
//
// La fuente es una tabla bitmap 5x7 propia (ver hud.cpp): evita depender de
// SDL2_ttf y de encontrar un archivo .ttf en el sistema.
#pragma once
#include <cstdint>
#include <vector>

struct MetricasHud {
    int    n_cuerpos  = 0;
    int    hilos      = 1;
    double fps        = 0.0;
    double ms_fisica  = 0.0;   // tiempo del bucle O(N^2) por frame
    double ms_render  = 0.0;   // tiempo de estelas + destellos por frame
    double energia    = 0.0;   // E = cinetica + potencial
    double drift_pct  = 0.0;   // desviacion vs la energia inicial
    const char* modo  = "";
    bool   pausado    = false;
    bool   paralelo   = false; // compilado con OpenMP?
};

// Dibuja el panel en la esquina superior izquierda del framebuffer.
void dibujar_hud(std::vector<uint32_t>& fb, int ancho, int alto,
                 const MetricasHud& m);
