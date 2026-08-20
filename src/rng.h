// rng.h
// -----------------------------------------------------------------------------
// Generador pseudoaleatorio xorshift32, con estado PRIVADO por instancia.
//
// Por que no usar rand()/srand() de <cstdlib>:
//   - rand() mantiene un unico estado GLOBAL. En la version paralela, todos
//     los hilos escribiendo/leyendo ese estado a la vez es una carrera de
//     datos (comportamiento indefinido) y ademas serializa el acceso si se
//     protege con un mutex/critical, matando el paralelismo.
//   - Con xorshift32 cada particula (o cada hilo) tiene su PROPIO estado.
//     No hay memoria compartida que proteger => no hace falta seccion
//     critica para generar numeros aleatorios.
//   - Ademas, sembrando cada estado como `semilla_global ^ indice` (o
//     `semilla_global ^ omp_get_thread_num()`), la simulacion es
//     REPRODUCIBLE: misma semilla -> mismos resultados, util para
//     depurar y para las mediciones del Anexo 3.
// -----------------------------------------------------------------------------
#pragma once
#include <cstdint>

struct Xorshift32 {
    uint32_t estado;

    explicit Xorshift32(uint32_t semilla) {
        // xorshift no puede arrancar en 0 (se quedaria en 0 para siempre).
        estado = (semilla == 0) ? 0xA5A5A5A5u : semilla;
    }

    // Devuelve el siguiente entero pseudoaleatorio de 32 bits.
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
