#include "palette.h"
#include <algorithm>

namespace {
// Rampa de 5 paradas: azul -> cian -> blanco -> amarillo -> rojo.
// Se interpola linealmente entre la parada k y la k+1.
constexpr int N_PARADAS = 5;
constexpr uint8_t PARADAS[N_PARADAS][3] = {
    {  20,  40, 160 },  // 0.00  azul profundo: orbitas externas, lentas
    {  60, 170, 230 },  // 0.25  cian
    { 245, 245, 255 },  // 0.50  blanco: rapidez tipica de orbita
    { 255, 205,  90 },  // 0.75  amarillo
    { 255, 100,  40 }   // 1.00  naranja/rojo: lo mas rapido de la escena
};

// Interpolacion lineal de un canal de color entre a y b.
inline uint8_t mezclar(uint8_t a, uint8_t b, float f) {
    return static_cast<uint8_t>(a + (b - a) * f);
}
} // namespace

ColorRGB color_por_rapidez(float t) {
    t = std::clamp(t, 0.0f, 1.0f);

    const float escalado = t * (N_PARADAS - 1);
    int k = static_cast<int>(escalado);
    if (k >= N_PARADAS - 1) k = N_PARADAS - 2;   // t == 1.0 cae en el ultimo tramo
    const float f = escalado - k;

    return { mezclar(PARADAS[k][0], PARADAS[k + 1][0], f),
             mezclar(PARADAS[k][1], PARADAS[k + 1][1], f),
             mezclar(PARADAS[k][2], PARADAS[k + 1][2], f) };
}
