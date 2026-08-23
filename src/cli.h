// cli.h -- Parseo y validacion de los argumentos de linea de comandos.
//
// Requisito del enunciado: cero variables hard-coded y programacion defensiva.
// Toda entrada invalida se rechaza con un mensaje claro ANTES de inicializar
// SDL o reservar memoria.
#pragma once
#include <cstdint>
#include <string>
#include "nbody.h"

struct Parametros {
    int      n_cuerpos = 5000;   // -n --particles   [2, 200000]
    int      ancho     = 1000;   // -w --width       [>= 640]
    int      alto      = 700;    // -h --height      [>= 480]
    int      hilos     = 0;      // -t --threads     0 = automatico
    uint32_t semilla   = 42;     // -s --seed
    long     frames_max = 0;     // -f --frames      0 = infinito
    Modo     modo = Modo::Colision;  // -m --mode

    ParametrosFisica fisica;     // -G gravedad, -e softening, -d paso

    bool sin_estelas   = false;  // --no-trails
    bool mostrar_ayuda = false;  // --help
};

// Parsea argv en `out`. Si algo es invalido imprime el error en stderr y
// devuelve false; el llamador debe salir con codigo != 0.
bool parsear_argumentos(int argc, char** argv, Parametros& out);

void imprimir_ayuda(const char* nombre_programa);
