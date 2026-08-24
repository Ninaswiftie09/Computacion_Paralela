#include "hud.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace {

// --- Fuente bitmap 5x7 ---------------------------------------------------
// Cada glifo son 5 columnas; en cada columna el bit k encendido significa que
// el pixel de la fila k esta pintado (bit 0 = fila de arriba).
// La tabla arranca en el ASCII 32 (espacio) y llega al 95 ('_'), que cubre
// digitos, mayusculas y los simbolos que usa el panel.
constexpr int ASCII_BASE = 32;
constexpr int ANCHO_GLIFO = 5, ALTO_GLIFO = 7;

const uint8_t FUENTE[][ANCHO_GLIFO] = {
    {0x00,0x00,0x00,0x00,0x00}, // 32 espacio
    {0x00,0x00,0x5F,0x00,0x00}, // 33 !
    {0x00,0x07,0x00,0x07,0x00}, // 34 "
    {0x14,0x7F,0x14,0x7F,0x14}, // 35 #
    {0x24,0x2A,0x7F,0x2A,0x12}, // 36 $
    {0x23,0x13,0x08,0x64,0x62}, // 37 %
    {0x36,0x49,0x55,0x22,0x50}, // 38 &
    {0x00,0x05,0x03,0x00,0x00}, // 39 '
    {0x00,0x1C,0x22,0x41,0x00}, // 40 (
    {0x00,0x41,0x22,0x1C,0x00}, // 41 )
    {0x14,0x08,0x3E,0x08,0x14}, // 42 *
    {0x08,0x08,0x3E,0x08,0x08}, // 43 +
    {0x00,0x50,0x30,0x00,0x00}, // 44 ,
    {0x08,0x08,0x08,0x08,0x08}, // 45 -
    {0x00,0x60,0x60,0x00,0x00}, // 46 .
    {0x20,0x10,0x08,0x04,0x02}, // 47 /
    {0x3E,0x51,0x49,0x45,0x3E}, // 48 0
    {0x00,0x42,0x7F,0x40,0x00}, // 49 1
    {0x42,0x61,0x51,0x49,0x46}, // 50 2
    {0x21,0x41,0x45,0x4B,0x31}, // 51 3
    {0x18,0x14,0x12,0x7F,0x10}, // 52 4
    {0x27,0x45,0x45,0x45,0x39}, // 53 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 54 6
    {0x01,0x71,0x09,0x05,0x03}, // 55 7
    {0x36,0x49,0x49,0x49,0x36}, // 56 8
    {0x06,0x49,0x49,0x29,0x1E}, // 57 9
    {0x00,0x36,0x36,0x00,0x00}, // 58 :
    {0x00,0x56,0x36,0x00,0x00}, // 59 ;
    {0x08,0x14,0x22,0x41,0x00}, // 60 <
    {0x14,0x14,0x14,0x14,0x14}, // 61 =
    {0x00,0x41,0x22,0x14,0x08}, // 62 >
    {0x02,0x01,0x51,0x09,0x06}, // 63 ?
    {0x32,0x49,0x79,0x41,0x3E}, // 64 @
    {0x7E,0x11,0x11,0x11,0x7E}, // 65 A
    {0x7F,0x49,0x49,0x49,0x36}, // 66 B
    {0x3E,0x41,0x41,0x41,0x22}, // 67 C
    {0x7F,0x41,0x41,0x22,0x1C}, // 68 D
    {0x7F,0x49,0x49,0x49,0x41}, // 69 E
    {0x7F,0x09,0x09,0x09,0x01}, // 70 F
    {0x3E,0x41,0x49,0x49,0x7A}, // 71 G
    {0x7F,0x08,0x08,0x08,0x7F}, // 72 H
    {0x00,0x41,0x7F,0x41,0x00}, // 73 I
    {0x20,0x40,0x41,0x3F,0x01}, // 74 J
    {0x7F,0x08,0x14,0x22,0x41}, // 75 K
    {0x7F,0x40,0x40,0x40,0x40}, // 76 L
    {0x7F,0x02,0x0C,0x02,0x7F}, // 77 M
    {0x7F,0x04,0x08,0x10,0x7F}, // 78 N
    {0x3E,0x41,0x41,0x41,0x3E}, // 79 O
    {0x7F,0x09,0x09,0x09,0x06}, // 80 P
    {0x3E,0x41,0x51,0x21,0x5E}, // 81 Q
    {0x7F,0x09,0x19,0x29,0x46}, // 82 R
    {0x46,0x49,0x49,0x49,0x31}, // 83 S
    {0x01,0x01,0x7F,0x01,0x01}, // 84 T
    {0x3F,0x40,0x40,0x40,0x3F}, // 85 U
    {0x1F,0x20,0x40,0x20,0x1F}, // 86 V
    {0x3F,0x40,0x38,0x40,0x3F}, // 87 W
    {0x63,0x14,0x08,0x14,0x63}, // 88 X
    {0x07,0x08,0x70,0x08,0x07}, // 89 Y
    {0x61,0x51,0x49,0x45,0x43}, // 90 Z
    {0x00,0x7F,0x41,0x41,0x00}, // 91 [
    {0x02,0x04,0x08,0x10,0x20}, // 92 backslash
    {0x00,0x41,0x41,0x7F,0x00}, // 93 ]
    {0x04,0x02,0x01,0x02,0x04}, // 94 ^
    {0x40,0x40,0x40,0x40,0x40}, // 95 _
};
constexpr int N_GLIFOS = static_cast<int>(sizeof(FUENTE) / sizeof(FUENTE[0]));

constexpr int ESCALA   = 2;                       // cada pixel del glifo = 2x2
constexpr int AVANCE_X = (ANCHO_GLIFO + 1) * ESCALA;
constexpr int AVANCE_Y = (ALTO_GLIFO + 2) * ESCALA;
constexpr int MARGEN   = 8;

inline uint32_t empacar(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) | b;
}

// Escribe una linea de texto. Las minusculas se pasan a mayusculas; lo que no
// este en la tabla se dibuja como espacio.
void dibujar_texto(std::vector<uint32_t>& fb, int ancho, int alto,
                   int x, int y, const char* texto, uint32_t color) {
    for (const char* c = texto; *c; ++c, x += AVANCE_X) {
        int idx = static_cast<unsigned char>(*c);
        if (idx >= 'a' && idx <= 'z') idx -= 32;   // minuscula -> mayuscula
        idx -= ASCII_BASE;
        if (idx < 0 || idx >= N_GLIFOS) continue;

        for (int col = 0; col < ANCHO_GLIFO; ++col) {
            const uint8_t bits = FUENTE[idx][col];
            for (int fil = 0; fil < ALTO_GLIFO; ++fil) {
                if (!(bits & (1u << fil))) continue;
                // Un pixel del glifo se pinta como un bloque ESCALA x ESCALA.
                for (int dy = 0; dy < ESCALA; ++dy) {
                    const int py = y + fil * ESCALA + dy;
                    if (py < 0 || py >= alto) continue;
                    uint32_t* fila = fb.data() + static_cast<size_t>(py) * ancho;
                    for (int dx = 0; dx < ESCALA; ++dx) {
                        const int px = x + col * ESCALA + dx;
                        if (px < 0 || px >= ancho) continue;
                        fila[px] = color;
                    }
                }
            }
        }
    }
}

// Oscurece un rectangulo para que el texto se lea sobre cualquier fondo.
void atenuar_recuadro(std::vector<uint32_t>& fb, int ancho, int alto,
                      int x0, int y0, int w, int h) {
    const int x1 = std::min(ancho, x0 + w), y1 = std::min(alto, y0 + h);
    for (int y = std::max(0, y0); y < y1; ++y) {
        uint32_t* fila = fb.data() + static_cast<size_t>(y) * ancho;
        for (int x = std::max(0, x0); x < x1; ++x) {
            const uint32_t c = fila[x];
            fila[x] = 0xFF000000u |
                      ((((c >> 16) & 0xFF) >> 2) << 16) |
                      ((((c >>  8) & 0xFF) >> 2) <<  8) |
                      (( (c        & 0xFF) >> 2));
        }
    }
}

} // namespace

void dibujar_hud(std::vector<uint32_t>& fb, int ancho, int alto,
                 const MetricasHud& m) {
    char lineas[5][64];
    std::snprintf(lineas[0], sizeof(lineas[0]), "N=%d  HILOS=%d  FPS=%.1f",
                  m.n_cuerpos, m.hilos, m.fps);
    std::snprintf(lineas[1], sizeof(lineas[1]), "FISICA=%.2fMS  RENDER=%.2fMS",
                  m.ms_fisica, m.ms_render);
    std::snprintf(lineas[2], sizeof(lineas[2]), "ENERGIA=%.4E  DRIFT=%.3f%%",
                  m.energia, m.drift_pct);
    std::snprintf(lineas[3], sizeof(lineas[3]), "MODO=%s  %s%s",
                  m.modo, m.paralelo ? "OPENMP" : "SECUENCIAL",
                  m.pausado ? "  [PAUSA]" : "");
    std::snprintf(lineas[4], sizeof(lineas[4]), "1-9 HILOS  R M T  ESC");

    constexpr int N_LINEAS = 5;
    int ancho_max = 0;
    for (int i = 0; i < N_LINEAS; ++i)
        ancho_max = std::max(ancho_max, static_cast<int>(std::strlen(lineas[i])));

    atenuar_recuadro(fb, ancho, alto, MARGEN / 2, MARGEN / 2,
                     ancho_max * AVANCE_X + MARGEN, N_LINEAS * AVANCE_Y + MARGEN);

    // La linea de FPS en verde si cumple los 30 que pide el enunciado, roja si no.
    const uint32_t VERDE = empacar(120, 255, 140);
    const uint32_t ROJO  = empacar(255, 110,  90);
    const uint32_t GRIS  = empacar(190, 200, 220);

    for (int i = 0; i < N_LINEAS; ++i) {
        const uint32_t color = (i == 0) ? (m.fps >= 30.0 ? VERDE : ROJO) : GRIS;
        dibujar_texto(fb, ancho, alto, MARGEN, MARGEN + i * AVANCE_Y, lineas[i], color);
    }
}
