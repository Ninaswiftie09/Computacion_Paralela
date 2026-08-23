#include "nbody.h"
#include <cmath>
#include <algorithm>

namespace {
constexpr float PI = 3.14159265358979323846f;

// --- Constantes de escena (no son la fisica, solo como se siembra) ---
// La masa TOTAL de cada galaxia es fija e independiente de N: si N cambia, cada
// estrella pesa menos y la dinamica se ve igual. Eso hace que el benchmark
// compare peras con peras al barrer N.
constexpr float MASA_GALAXIA    = 6000.0f;
constexpr float FRACCION_NUCLEO = 0.85f;  // el nucleo domina: orbitas limpias y estables
constexpr float RADIO_DISCO_REL = 0.30f;  // radio del disco / min(ancho, alto)
constexpr float RADIO_MIN_REL   = 0.05f;  // agujero central: nada nace sobre el nucleo
constexpr float VEL_ACERCAMIENTO = 28.0f; // px/s con que se embisten las galaxias
} // namespace

bool modo_desde_texto(const std::string& texto, Modo& out) {
    if (texto == "colision") { out = Modo::Colision; return true; }
    if (texto == "galaxia")  { out = Modo::Galaxia;  return true; }
    if (texto == "nube")     { out = Modo::Nube;     return true; }
    return false;
}

const char* texto_de_modo(Modo m) {
    switch (m) {
        case Modo::Colision: return "colision";
        case Modo::Galaxia:  return "galaxia";
        case Modo::Nube:     return "nube";
    }
    return "?";
}

SistemaNCuerpos::SistemaNCuerpos(int n, int ancho, int alto, Modo modo,
                                 uint32_t semilla, const ParametrosFisica& fisica)
    : n_(n), ancho_(ancho), alto_(alto), fisica_(fisica) {
    // resize() lanza std::bad_alloc si N es absurdo; main.cpp lo atrapa.
    px_.resize(n); py_.resize(n);
    vx_.resize(n); vy_.resize(n);
    ax_.resize(n); ay_.resize(n);
    masa_.resize(n);
    reiniciar(modo, semilla);
}

void SistemaNCuerpos::sembrar_disco(Xorshift32& r, int desde, int cuenta,
                                    float cx, float cy,
                                    float vx_bulk, float vy_bulk, int giro) {
    if (cuenta <= 0) return;

    const float lado    = static_cast<float>(std::min(ancho_, alto_));
    const float radio   = RADIO_DISCO_REL * lado;
    const float r_min   = RADIO_MIN_REL   * lado;
    const float m_nucleo = MASA_GALAXIA * FRACCION_NUCLEO;
    const float m_disco  = MASA_GALAXIA - m_nucleo;
    const int   estrellas = cuenta - 1;
    const float G = static_cast<float>(fisica_.gravedad);

    // Cuerpo 0 de la galaxia: el nucleo, quieto respecto a su propia galaxia.
    px_[desde] = cx;  py_[desde] = cy;
    vx_[desde] = vx_bulk;  vy_[desde] = vy_bulk;
    masa_[desde] = m_nucleo;

    if (estrellas <= 0) return;
    const float m_estrella = m_disco / static_cast<float>(estrellas);

    for (int k = 0; k < estrellas; ++k) {
        const int i = desde + 1 + k;

        // r = r_min + (R - r_min)*u reparte los cuerpos con densidad superficial
        // proporcional a 1/r: concentrados al centro y ralos en el borde, que es
        // como se ve una galaxia de verdad.
        const float ang = r.rango(0.0f, 2.0f * PI);
        const float rad = r_min + (radio - r_min) * r.uniforme01();

        px_[i] = cx + std::cos(ang) * rad;
        py_[i] = cy + std::sin(ang) * rad;
        masa_[i] = m_estrella;

        // Velocidad circular usando la masa ENCERRADA dentro del radio rad
        // (nucleo + la parte del disco que queda adentro): es la curva de
        // rotacion de la galaxia, no solo la atraccion del nucleo.
        // Con densidad ~1/r la masa encerrada crece lineal con el radio.
        const float m_enc = m_nucleo + m_disco * (rad / radio);
        const float v = std::sqrt(G * m_enc / rad);

        // Tangencial = perpendicular al radio. Aqui entra la trigonometria.
        vx_[i] = vx_bulk + giro * (-std::sin(ang)) * v;
        vy_[i] = vy_bulk + giro * ( std::cos(ang)) * v;
    }
}

void SistemaNCuerpos::sembrar_nube(Xorshift32& r) {
    // Sin nucleo ni rotacion: masas iguales, casi en reposo. La gravedad sola
    // las colapsa en filamentos y cumulos.
    const float m = MASA_GALAXIA / static_cast<float>(n_);
    const float mx = ancho_ * 0.15f, my = alto_ * 0.15f;
    for (int i = 0; i < n_; ++i) {
        px_[i] = r.rango(mx, ancho_ - mx);
        py_[i] = r.rango(my, alto_  - my);
        vx_[i] = r.rango(-6.0f, 6.0f);
        vy_[i] = r.rango(-6.0f, 6.0f);
        masa_[i] = m;
    }
}

void SistemaNCuerpos::reiniciar(Modo modo, uint32_t semilla) {
    Xorshift32 r(semilla);
    std::fill(ax_.begin(), ax_.end(), 0.0f);
    std::fill(ay_.begin(), ay_.end(), 0.0f);

    const float w = static_cast<float>(ancho_), h = static_cast<float>(alto_);

    switch (modo) {
        case Modo::Colision: {
            // Dos galaxias desplazadas en diagonal y con velocidades opuestas:
            // no chocan de frente, se rozan -> se forman colas de marea.
            const int n_a = n_ / 2;
            const int n_b = n_ - n_a;
            sembrar_disco(r, 0,   n_a, w * 0.28f, h * 0.30f,
                           VEL_ACERCAMIENTO,  VEL_ACERCAMIENTO * 0.35f, +1);
            sembrar_disco(r, n_a, n_b, w * 0.72f, h * 0.70f,
                          -VEL_ACERCAMIENTO, -VEL_ACERCAMIENTO * 0.35f, -1);
            break;
        }
        case Modo::Galaxia:
            sembrar_disco(r, 0, n_, w * 0.5f, h * 0.5f, 0.0f, 0.0f, +1);
            break;
        case Modo::Nube:
            sembrar_nube(r);
            break;
    }

    // Escala de referencia para el color: la velocidad circular en el borde del
    // disco. Fija y reproducible, asi el color no parpadea entre frames.
    const float lado = static_cast<float>(std::min(ancho_, alto_));
    rapidez_ref_ = std::sqrt(static_cast<float>(fisica_.gravedad) * MASA_GALAXIA /
                             (RADIO_DISCO_REL * lado)) * 2.4f;
    if (rapidez_ref_ < 1.0f) rapidez_ref_ = 1.0f; // nunca dividir entre ~0
}

void SistemaNCuerpos::calcular_aceleraciones() {
    const float G    = static_cast<float>(fisica_.gravedad);
    const float eps2 = static_cast<float>(fisica_.softening * fisica_.softening);

    // --- El bucle O(N^2): el 95% del tiempo de CPU del programa ---
    // El bucle externo es 100% independiente entre iteraciones (cada i escribe
    // solo ax_[i]/ay_[i] y solo LEE px_/py_/masa_), asi que paralelizarlo no
    // requiere ninguna proteccion de memoria compartida.
    for (int i = 0; i < n_; ++i) {
        const float xi = px_[i], yi = py_[i];
        float acc_x = 0.0f, acc_y = 0.0f;

        for (int j = 0; j < n_; ++j) {
            const float dx = px_[j] - xi;
            const float dy = py_[j] - yi;

            // Softening de Plummer: el eps^2 evita que la fuerza explote cuando
            // dos cuerpos casi se tocan. Ademas hace que el caso j == i aporte
            // exactamente 0, asi que no hace falta un `if (i != j)` que rompa
            // la vectorizacion del bucle interno.
            const float r2 = dx * dx + dy * dy + eps2;
            const float inv_r = 1.0f / std::sqrt(r2);
            const float inv_r3 = inv_r * inv_r * inv_r;   // 1/r^3, sin pow()

            const float p = masa_[j] * inv_r3;
            acc_x += dx * p;
            acc_y += dy * p;
        }

        ax_[i] = G * acc_x;
        ay_[i] = G * acc_y;
    }
}

void SistemaNCuerpos::integrar() {
    const float dt = static_cast<float>(fisica_.paso);

    // Euler semi-implicito: se actualiza v con la aceleracion nueva y LUEGO se
    // mueve p con la v ya actualizada. Conserva la energia mucho mejor que el
    // Euler explicito, con el mismo costo.
    for (int i = 0; i < n_; ++i) {
        vx_[i] += ax_[i] * dt;
        vy_[i] += ay_[i] * dt;
        px_[i] += vx_[i] * dt;
        py_[i] += vy_[i] * dt;
    }
}

double SistemaNCuerpos::energia_cinetica() const {
    double ke = 0.0;
    for (int i = 0; i < n_; ++i) {
        ke += 0.5 * masa_[i] * (static_cast<double>(vx_[i]) * vx_[i] +
                                static_cast<double>(vy_[i]) * vy_[i]);
    }
    return ke;
}

double SistemaNCuerpos::energia_potencial() const {
    const double G = fisica_.gravedad;
    const double eps2 = fisica_.softening * fisica_.softening;
    double pe = 0.0;

    // Potencial de Plummer: U = -G*m_i*m_j / sqrt(r^2 + eps^2). Es exactamente
    // el potencial cuyo gradiente da la fuerza suavizada de arriba, por eso
    // KE + PE si tiene sentido como cantidad conservada.
    // Cada par se cuenta una sola vez (j > i).
    for (int i = 0; i < n_; ++i) {
        for (int j = i + 1; j < n_; ++j) {
            const double dx = static_cast<double>(px_[j]) - px_[i];
            const double dy = static_cast<double>(py_[j]) - py_[i];
            pe -= G * masa_[i] * masa_[j] / std::sqrt(dx * dx + dy * dy + eps2);
        }
    }
    return pe;
}
