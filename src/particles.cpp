// particles.cpp
#include "particles.h"
#include <cmath>
#include <algorithm>

namespace {
    constexpr float PI = 3.14159265358979323846f;

    // --- constantes del modelo fisico (ver docs/PROPUESTA.md seccion 2) ---
    constexpr float C_TAYLOR   = 1.03f;   // constante de Taylor-Sedov para aire
    constexpr float GAMMA_AIRE = 1.4f;
    constexpr float RHO_AIRE   = 1.2f;    // kg/m3 (escalado libremente a px, no es SI real)

    constexpr float GRAVEDAD      = 220.0f;   // px/s^2
    constexpr float COEF_ARRASTRE = 0.6f;     // arrastre cuadratico
    constexpr float COEF_FLOTACION = 140.0f;  // empuje termico (forma el tallo del hongo)
    constexpr float RESTITUCION_SUELO = 0.4f; // "e" del rebote

    constexpr float LAMBDA_ATEN = 260.0f;     // px, atenuacion del impulso con la distancia
    constexpr float K_IMPULSO   = 5.5f;
    constexpr float ALPHA_T     = 0.9f;       // cuanto calienta el impulso a la particula

    constexpr float VORTICE_GAMMA = 9000.0f;  // circulacion del anillo del hongo
    constexpr float VORTICE_RADIO = 70.0f;
    constexpr float VORTICE_EPS   = 8.0f;
}

// ------------------------------- Escena ------------------------------------

Escena::Escena(int ancho_, int alto_, double energia_kt_, uint32_t semilla_)
    : energia_kt(energia_kt_), semilla(semilla_), ancho(ancho_), alto(alto_) {
    centro_x = ancho * 0.5f;
    centro_y = alto * 0.35f;     // la detonacion ocurre en el aire, no al ras del piso
    suelo_y  = alto * 0.85f;
}

double Escena::radio_frente(double tiempo) const {
    if (tiempo <= 0.0) return 0.0;
    // R(t) = C * (E/rho)^(1/5) * t^(2/5)  -- solucion de Taylor-von Neumann-Sedov.
    // La energia se escala a un rango que produzca radios visibles en pantalla
    // (esto es una simulacion visual, no un calculo de mega-toneladas real).
    double E_escalada = energia_kt * 4000.0;
    double base = std::pow(E_escalada / RHO_AIRE, 1.0 / 5.0);
    return C_TAYLOR * base * std::pow(tiempo, 2.0 / 5.0);
}

Fase Escena::fase_actual() const {
    if (t < 0.3)  return Fase::Destello;
    if (t < 2.0)  return Fase::BolaDeFuego;
    if (t < 8.0)  return Fase::Escombros;   // onda de choque + escombros se traslapan
    return Fase::Hongo;
}

// --------------------------- SistemaParticulas ------------------------------

SistemaParticulas::SistemaParticulas(int n, const Escena& escena) : n_(n) {
    px_.resize(n); py_.resize(n);
    vx_.resize(n); vy_.resize(n);
    temp_.resize(n);
    enfriamiento_.resize(n);
    fase_ang_.resize(n);
    viva_.resize(n, 1);
    rng_.reserve(n);
    for (int i = 0; i < n; ++i) {
        // Semilla por particula = semilla global ^ indice: reproducible y,
        // en la version paralela, sin estado compartido entre hilos.
        rng_.emplace_back(escena.semilla ^ static_cast<uint32_t>(i * 2654435761u));
    }
    for (int i = 0; i < n; ++i) {
        inicializar_particula(i, escena);
    }
}

void SistemaParticulas::inicializar_particula(int i, const Escena& escena) {
    Xorshift32& r = rng_[i];

    // Nace cerca del centro de la detonacion, en un radio pequeno.
    float ang = r.rango(0.0f, 2.0f * PI);
    float r0  = r.rango(0.0f, 18.0f);
    px_[i] = escena.centro_x + std::cos(ang) * r0;
    py_[i] = escena.centro_y + std::sin(ang) * r0;

    // Velocidad inicial pequena y radial (el impulso grande llega despues,
    // via el impulso de la onda de choque en actualizar()).
    float v0 = r.rango(5.0f, 30.0f);
    vx_[i] = std::cos(ang) * v0;
    vy_[i] = std::sin(ang) * v0 - r.rango(0.0f, 10.0f);

    temp_[i] = r.rango(0.85f, 1.0f);              // nace muy caliente
    enfriamiento_[i] = r.rango(0.05f, 0.18f);      // jitter: no todas se enfrian igual
    fase_ang_[i] = r.rango(0.0f, 2.0f * PI);
    viva_[i] = 1;
}

void SistemaParticulas::actualizar(Escena& escena, float dt) {
    escena.t += dt;
    const double R = escena.radio_frente(escena.t);
    const float cx = escena.centro_x;
    const float cy = escena.centro_y;

    // ---- bucle independiente por particula: candidato a parallel for ----
    for (int i = 0; i < n_; ++i) {
        if (!viva_[i]) continue;

        float dx = px_[i] - cx;
        float dy = py_[i] - cy;
        float dist = std::sqrt(dx * dx + dy * dy) + 1e-4f;
        float ux = dx / dist;
        float uy = dy / dist;

        // Impulso de la onda de choque: se aplica cuando el frente R(t)
        // esta pasando cerca de la particula (ver docs/PROPUESTA.md 2.2).
        float dif = static_cast<float>(dist - R);
        if (std::fabs(dif) < 40.0f) {
            float dP = 1.0f / (1.0f + static_cast<float>(R * R * R) * 1e-6f); // ~ R^-3
            float J = K_IMPULSO * dP * std::exp(-dist / LAMBDA_ATEN);
            vx_[i] += J * ux;
            vy_[i] += J * uy;
            temp_[i] = std::min(1.0f, temp_[i] + ALPHA_T * J * 0.01f);
        }

        // Arrastre cuadratico: -c_d * |v| * v
        float speed = std::sqrt(vx_[i] * vx_[i] + vy_[i] * vy_[i]);
        float ax = -COEF_ARRASTRE * speed * vx_[i] * 0.01f;
        float ay = -COEF_ARRASTRE * speed * vy_[i] * 0.01f;

        // Gravedad
        ay += GRAVEDAD;

        // Flotacion: el aire caliente sube (empuje proporcional a temperatura)
        ay -= COEF_FLOTACION * temp_[i];

        // Vortice toroidal (fase Hongo): enrolla el sombrero del hongo.
        if (escena.fase_actual() == Fase::Hongo) {
            float h = cy - static_cast<float>(escena.alto) * 0.25f; // altura del anillo, sube con el tiempo (aprox.)
            float theta = std::atan2(py_[i] - h, px_[i] - cx);
            float r_local = std::max(VORTICE_EPS,
                std::sqrt((px_[i] - cx) * (px_[i] - cx) + (py_[i] - h) * (py_[i] - h)));
            float omega = VORTICE_GAMMA / (2.0f * PI * r_local);
            ax += omega * (-std::sin(theta + fase_ang_[i] * 0.0f));
            ay += omega * ( std::cos(theta));
        }

        vx_[i] += ax * dt;
        vy_[i] += ay * dt;
        px_[i] += vx_[i] * dt;
        py_[i] += vy_[i] * dt;

        // Rebote contra el suelo
        if (py_[i] > escena.suelo_y) {
            py_[i] = escena.suelo_y;
            vy_[i] = -vy_[i] * RESTITUCION_SUELO;
        }

        // Enfriamiento
        temp_[i] -= enfriamiento_[i] * dt * 0.3f;
        if (temp_[i] < 0.0f) temp_[i] = 0.0f;

        // Se "apaga" si sale muy lejos del cuadro (se podria reciclar con un
        // nuevo ciclo; por simplicidad, en esta version solo se marca inactiva).
        if (px_[i] < -200 || px_[i] > escena.ancho + 200 ||
            py_[i] < -200) {
            viva_[i] = 0;
        }
    }
}

void acumular_resplandor(uint8_t* framebuffer, int ancho, int alto,
                          const SistemaParticulas& sp) {
    // Campo de resplandor por pixel: para cada particula "fuente" (caliente),
    // se le suma brillo a los pixeles cercanos. Es O(N * radio^2) y es la
    // carga computacional pesada que garantiza que el speedup se note
    // (ver docs/PROPUESTA.md seccion 6). Bucle sobre filas -> paralelizable
    // por bandas disjuntas de filas, sin necesidad de proteger memoria.
    const auto& px = sp.px();
    const auto& py = sp.py();
    const auto& temp = sp.temp();
    const auto& viva = sp.viva();

    const int RADIO = 14; // px de influencia de cada fuente

    for (int i = 0; i < sp.n(); ++i) {
        if (!viva[i]) continue;
        if (temp[i] < 0.05f) continue; // muy fria, no aporta resplandor perceptible

        ColorRGB c = color_por_temperatura(temp[i]);
        int cx = static_cast<int>(px[i]);
        int cy = static_cast<int>(py[i]);

        int y0 = std::max(0, cy - RADIO);
        int y1 = std::min(alto - 1, cy + RADIO);
        int x0 = std::max(0, cx - RADIO);
        int x1 = std::min(ancho - 1, cx + RADIO);

        for (int y = y0; y <= y1; ++y) {
            float dy = static_cast<float>(y - cy);
            uint8_t* fila = framebuffer + static_cast<size_t>(y) * ancho * 4;
            for (int x = x0; x <= x1; ++x) {
                float dx = static_cast<float>(x - cx);
                float d2 = dx * dx + dy * dy;
                float peso = 1.0f - (d2 / (RADIO * RADIO));
                if (peso <= 0.0f) continue;
                peso *= peso * temp[i]; // caida suave, escalada por temperatura

                uint8_t* px_out = fila + x * 4;
                px_out[0] = static_cast<uint8_t>(std::min(255.0f, px_out[0] + c.r * peso));
                px_out[1] = static_cast<uint8_t>(std::min(255.0f, px_out[1] + c.g * peso));
                px_out[2] = static_cast<uint8_t>(std::min(255.0f, px_out[2] + c.b * peso));
                px_out[3] = 255;
            }
        }
    }
}
