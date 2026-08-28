// nbody.h -- Simulacion de N cuerpos con gravitacion newtoniana.
//
// Toda la fisica del proyecto cabe en tres lineas:
//
//   F   = G * m_i * m_j / r^2                                (gravitacion universal)
//   a_i = G * SUM_j  m_j * (r_j - r_i) / (r_ij^2 + eps^2)^(3/2)
//   v += a*dt   ->   p += v*dt                               (Euler semi-implicito)
//
// El costo es O(N^2) por paso: cada cuerpo interactua con todos los demas.
// Ese es justamente el motivo por el que vale la pena paralelizar.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "rng.h"

// Escenario inicial. La fisica es identica en los tres: solo cambia como se
// siembran posiciones, velocidades y masas.
enum class Modo {
    Colision,  // dos galaxias en curso de choque (el mas vistoso)
    Galaxia,   // un solo disco en rotacion alrededor de un nucleo masivo
    Nube       // N cuerpos al azar, casi en reposo: colapso gravitacional puro
};

// Convierte "colision"/"galaxia"/"nube" a Modo. Devuelve false si no coincide.
bool modo_desde_texto(const std::string& texto, Modo& out);

// Texto legible de un Modo, para el HUD y el CSV de --bench.
const char* texto_de_modo(Modo m);

// Constantes fisicas ajustables desde la linea de comandos.
struct ParametrosFisica {
    double gravedad  = 100.0;  // G, escalada al espacio de pixeles
    double softening = 15.0;   // eps, en px (ver nota en calcular_aceleraciones)
    double paso      = 0.020;  // dt, segundos de simulacion por paso
};

class SistemaNCuerpos {
public:
    // Reserva los arreglos y siembra el escenario. Lanza std::bad_alloc si N
    // es tan grande que no cabe en memoria (el llamador debe atraparlo).
    SistemaNCuerpos(int n, int ancho, int alto, Modo modo,
                    uint32_t semilla, const ParametrosFisica& fisica);

    // Vuelve a sembrar el escenario sin reasignar memoria.
    void reiniciar(Modo modo, uint32_t semilla);

    // FASE 1: calcula la aceleracion de cada cuerpo. Solo LEE posiciones y masas.
    void calcular_aceleraciones();

    // FASE 2: aplica v += a*dt y p += v*dt. ESCRIBE posiciones y velocidades.
    //
    // Las dos fases estan separadas a proposito: si se fusionaran, en paralelo un
    // hilo moveria el cuerpo i mientras otro todavia necesita su posicion vieja
    // para calcular su propia fuerza. La barrera implicita entre ambas es el
    // mecanismo de sincronia del programa.
    void integrar();

    double energia_cinetica()  const;  // O(N)
    double energia_potencial() const;  // O(N^2): no llamarla cada frame

    int   n()          const { return n_; }
    float rapidez_ref() const { return rapidez_ref_; }  // escala para el color

    const std::vector<float>& px() const { return px_; }
    const std::vector<float>& py() const { return py_; }
    const std::vector<float>& vx() const { return vx_; }
    const std::vector<float>& vy() const { return vy_; }

private:
    int n_, ancho_, alto_;
    ParametrosFisica fisica_;
    float rapidez_ref_ = 1.0f;

    // SoA (Structure of Arrays) en vez de AoS: al recorrer un solo atributo para
    // todos los cuerpos la cache se aprovecha mejor, se evita el false sharing
    // entre hilos, y el bucle interno se puede vectorizar.
    std::vector<float> px_, py_;   // posicion
    std::vector<float> vx_, vy_;   // velocidad
    std::vector<float> ax_, ay_;   // aceleracion (salida de la fase 1)
    std::vector<float> masa_;

    // Siembra `cuenta` cuerpos como un disco en rotacion alrededor de (cx, cy),
    // con un nucleo masivo en el indice `desde`. `giro` = +1 o -1.
    void sembrar_disco(Xorshift32& r, int desde, int cuenta,
                       float cx, float cy, float vx_bulk, float vy_bulk, int giro);

    void sembrar_nube(Xorshift32& r);
};
