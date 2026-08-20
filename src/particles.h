// particles.h
// -----------------------------------------------------------------------------
// Sistema de N particulas para el screensaver "Detonacion".
//
// Layout SoA (Structure of Arrays) en vez de AoS (Array of Structs) a
// proposito: mejor localidad de cache al recorrer un solo atributo para
// todas las particulas, evita false sharing entre hilos cuando se
// paralelice, y facilita auto-vectorizacion. Ver docs/PROPUESTA.md seccion 3.
//
// Esta es la version SECUENCIAL (avance del proyecto). Los bucles de
// `actualizar()` y `acumular_resplandor()` estan escritos deliberadamente
// como bucles `for` planos e independientes entre iteraciones -- son el
// candidato directo para `#pragma omp parallel for` en la version paralela
// (ver PCAM en docs/PROPUESTA.md seccion 4). Por ahora corren en un solo hilo.
// -----------------------------------------------------------------------------
#pragma once
#include <cstdint>
#include <vector>
#include "palette.h"
#include "rng.h"

// Fase global de la detonacion. El tiempo de simulacion decide la fase
// activa; varias fases se traslapan (ver docs/PROPUESTA.md seccion 1).
enum class Fase {
    Destello,
    BolaDeFuego,
    OndaDeChoque,
    Escombros,
    Hongo
};

struct Escena {
    // --- parametros fisicos derivados de los argumentos CLI ---
    double energia_kt;     // energia de la explosion, en kilotones
    float  centro_x, centro_y;   // punto de la detonacion (coordenadas de pantalla)
    float  suelo_y;               // altura (en px) del piso, para el rebote de escombros

    // --- estado temporal ---
    double t = 0.0;               // segundos de simulacion transcurridos
    uint32_t semilla;

    int ancho, alto;

    Escena(int ancho_, int alto_, double energia_kt_, uint32_t semilla_);

    // Radio de la bola de fuego / frente de choque en el instante t,
    // segun la solucion autosemejante de Taylor-von Neumann-Sedov:
    //   R(t) = C * (E/rho)^(1/5) * t^(2/5)
    double radio_frente(double t) const;

    Fase fase_actual() const;
};

class SistemaParticulas {
public:
    explicit SistemaParticulas(int n, const Escena& escena);

    // Avanza la simulacion un paso `dt` (segundos). Bucle independiente
    // por particula -> candidato a #pragma omp parallel for.
    void actualizar(Escena& escena, float dt);

    // Dibuja cada particula como un pixel/rectangulo pequeno directamente
    // sobre el framebuffer de SDL (fuera de este archivo, en main.cpp).
    int n() const { return n_; }

    // Acceso de solo lectura para el renderer.
    const std::vector<float>& px() const { return px_; }
    const std::vector<float>& py() const { return py_; }
    const std::vector<float>& temp() const { return temp_; }
    const std::vector<uint8_t>& viva() const { return viva_; }

private:
    int n_;
    // --- SoA: un arreglo por atributo, todos de tamano n_ ---
    std::vector<float>   px_, py_;      // posicion
    std::vector<float>   vx_, vy_;      // velocidad
    std::vector<float>   temp_;         // temperatura normalizada [0,1]
    std::vector<float>   enfriamiento_; // tasa de enfriamiento propia (jitter)
    std::vector<float>   fase_ang_;     // fase angular propia, para el vortice del hongo
    std::vector<uint8_t> viva_;         // 0/1: si ya salio del cuadro o se apago

    // Cada particula lleva su PROPIO generador (ver rng.h): nada de estado
    // aleatorio compartido, asi que no hace falta seccion critica para el RNG.
    std::vector<Xorshift32> rng_;

    void inicializar_particula(int i, const Escena& escena);
};

// Acumula el "campo de resplandor" por pixel a partir de las fuentes
// (particulas calientes) cercanas, y lo mezcla sobre `framebuffer`
// (formato RGBA8888, tamano ancho*alto*4 bytes). Bucle sobre filas,
// independiente entre bandas de filas -> candidato a paralelizar por
// bandas disjuntas (ver docs/PROPUESTA.md seccion 4, sin necesidad de
// `critical`, cada hilo escribe su propia banda).
void acumular_resplandor(uint8_t* framebuffer, int ancho, int alto,
                          const SistemaParticulas& sp);
