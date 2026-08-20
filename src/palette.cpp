// palette.cpp
#include "palette.h"
#include <algorithm>
#include <cmath>

namespace {

struct Parada { float t; ColorRGB c; };

// Puntos de control de la rampa, de frio (0) a caliente (1).
const Parada PARADAS[] = {
    {0.00f, {40,  40,  45 }},  // humo gris oscuro (particula ya fria)
    {0.20f, {90,  70,  60 }},  // humo tibio
    {0.40f, {200, 60,  20 }},  // rojo
    {0.60f, {235, 120, 20 }},  // naranja
    {0.80f, {255, 200, 60 }},  // amarillo
    {1.00f, {255, 255, 245}},  // blanco (nucleo recien detonado)
};
const int N_PARADAS = sizeof(PARADAS) / sizeof(PARADAS[0]);

inline uint8_t lerp_u8(uint8_t a, uint8_t b, float f) {
    return static_cast<uint8_t>(a + (static_cast<float>(b) - a) * f);
}

} // namespace

ColorRGB color_por_temperatura(float t) {
    t = std::clamp(t, 0.0f, 1.0f);

    for (int i = 0; i < N_PARADAS - 1; ++i) {
        const Parada& a = PARADAS[i];
        const Parada& b = PARADAS[i + 1];
        if (t >= a.t && t <= b.t) {
            float rango = b.t - a.t;
            float f = (rango > 1e-6f) ? (t - a.t) / rango : 0.0f;
            return ColorRGB{
                lerp_u8(a.c.r, b.c.r, f),
                lerp_u8(a.c.g, b.c.g, f),
                lerp_u8(a.c.b, b.c.b, f)
            };
        }
    }
    return PARADAS[N_PARADAS - 1].c;
}
