// rng.h -- Generador pseudoaleatorio xorshift32 con estado PRIVADO por instancia.
//
// Por que no rand(): rand() tiene un unico estado GLOBAL. En la version paralela
// eso seria una condicion de carrera, y protegerlo con un critical serializaria
// el acceso. Aqui cada particula lleva su propio estado, asi que no hay memoria
// compartida que proteger. Ademas, sembrando como `semilla ^ indice`, la escena
// es reproducible: misma semilla -> misma simulacion.
#pragma once
#include <cstdint>

struct Xorshift32 {
    uint32_t estado;

    explicit Xorshift32(uint32_t semilla) {
        // xorshift no puede arrancar en 0 (se quedaria en 0 para siempre).
        estado = (semilla == 0) ? 0xA5A5A5A5u : semilla;
    }

    inline uint32_t siguiente() {
        uint32_t x = estado;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        estado = x;
        return x;
    }

    // Flotante uniforme en [0, 1).
    inline float uniforme01() {
        return (siguiente() >> 8) * (1.0f / 16777216.0f); // 24 bits de mantisa
    }

    // Flotante uniforme en [lo, hi).
    inline float rango(float lo, float hi) {
        return lo + uniforme01() * (hi - lo);
    }
};
