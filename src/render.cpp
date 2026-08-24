#include "render.h"
#include "palette.h"
#include <cmath>
#include <algorithm>
#ifdef _OPENMP
#include <omp.h>
#endif

namespace {
constexpr int RADIO_DESTELLO = 2;  // px; el destello ocupa (2R+1)^2 = 25 pixeles

// Suma color a un pixel ARGB8888 saturando en 255 (blend aditivo).
inline uint32_t sumar_saturando(uint32_t dst, int r, int g, int b) {
    const int nr = std::min(255, static_cast<int>((dst >> 16) & 0xFF) + r);
    const int ng = std::min(255, static_cast<int>((dst >>  8) & 0xFF) + g);
    const int nb = std::min(255, static_cast<int>( dst        & 0xFF) + b);
    return 0xFF000000u | (nr << 16) | (ng << 8) | nb;
}

// Pesos del destello precalculados: (1 - d^2/R^2)^2, con el centro a 1.0.
struct NucleoDestello {
    float peso[(2 * RADIO_DESTELLO + 1) * (2 * RADIO_DESTELLO + 1)];
    NucleoDestello() {
        const float r2 = static_cast<float>(RADIO_DESTELLO * RADIO_DESTELLO);
        for (int dy = -RADIO_DESTELLO; dy <= RADIO_DESTELLO; ++dy) {
            for (int dx = -RADIO_DESTELLO; dx <= RADIO_DESTELLO; ++dx) {
                float d2 = static_cast<float>(dx * dx + dy * dy);
                float w = 1.0f - d2 / (r2 + 1.0f);
                if (w < 0.0f) w = 0.0f;
                peso[(dy + RADIO_DESTELLO) * (2 * RADIO_DESTELLO + 1) +
                     (dx + RADIO_DESTELLO)] = w * w;
            }
        }
    }
};
const NucleoDestello NUCLEO;   // se construye una sola vez al arrancar
} // namespace

void desvanecer(std::vector<uint32_t>& fb, uint32_t factor) {
    const long total = static_cast<long>(fb.size());
    // Cada pixel es independiente del resto: paralelizar es directo.
    #pragma omp parallel for schedule(static)
    for (long i = 0; i < total; ++i) {
        const uint32_t c = fb[i];
        const uint32_t r = (((c >> 16) & 0xFF) * factor) >> 8;
        const uint32_t g = (((c >>  8) & 0xFF) * factor) >> 8;
        const uint32_t b = (( c        & 0xFF) * factor) >> 8;
        fb[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

void limpiar(std::vector<uint32_t>& fb, uint32_t color) {
    std::fill(fb.begin(), fb.end(), color);
}

void dibujar_cuerpos(std::vector<uint32_t>& fb, int ancho, int alto,
                     const SistemaNCuerpos& sistema) {
    const auto& px = sistema.px();
    const auto& py = sistema.py();
    const auto& vx = sistema.vx();
    const auto& vy = sistema.vy();
    const int   n  = sistema.n();
    const float inv_ref = 1.0f / sistema.rapidez_ref();
    constexpr int LADO = 2 * RADIO_DESTELLO + 1;

    // Dos cuerpos cercanos escriben los mismos pixeles, asi que paralelizar por
    // cuerpo seria una carrera de datos. En vez de eso se reparte la PANTALLA:
    // cada hilo es dueno de una banda de filas y recorta ahi su dibujo. Las
    // escrituras quedan disjuntas por construccion -- sin atomic ni critical.
    #pragma omp parallel
    {
#ifdef _OPENMP
        const int n_hilos = omp_get_num_threads();
        const int id      = omp_get_thread_num();
#else
        const int n_hilos = 1;
        const int id      = 0;
#endif
        const int banda_ini = (alto * id)       / n_hilos;
        const int banda_fin = (alto * (id + 1)) / n_hilos;   // exclusivo

        for (int i = 0; i < n; ++i) {
            const int cx = static_cast<int>(px[i]);
            const int cy = static_cast<int>(py[i]);

            // Descarte temprano: fuera del canvas, o fuera de la banda propia.
            if (cx < -RADIO_DESTELLO || cx >= ancho + RADIO_DESTELLO) continue;
            if (cy + RADIO_DESTELLO < banda_ini || cy - RADIO_DESTELLO >= banda_fin) continue;

            const float rapidez = std::sqrt(vx[i] * vx[i] + vy[i] * vy[i]);
            const ColorRGB c = color_por_rapidez(rapidez * inv_ref);

            const int y0 = std::max(banda_ini,     cy - RADIO_DESTELLO);
            const int y1 = std::min(banda_fin - 1, cy + RADIO_DESTELLO);
            const int x0 = std::max(0,         cx - RADIO_DESTELLO);
            const int x1 = std::min(ancho - 1, cx + RADIO_DESTELLO);

            for (int y = y0; y <= y1; ++y) {
                const int fila_nucleo = (y - cy + RADIO_DESTELLO) * LADO;
                uint32_t* fila = fb.data() + static_cast<size_t>(y) * ancho;
                for (int x = x0; x <= x1; ++x) {
                    const float w = NUCLEO.peso[fila_nucleo + (x - cx + RADIO_DESTELLO)];
                    if (w <= 0.0f) continue;
                    fila[x] = sumar_saturando(fila[x],
                                              static_cast<int>(c.r * w),
                                              static_cast<int>(c.g * w),
                                              static_cast<int>(c.b * w));
                }
            }
        }
    }
}
