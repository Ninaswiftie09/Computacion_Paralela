// cli.h
// -----------------------------------------------------------------------------
// Parseo y validacion de argumentos de linea de comandos.
// Requisito del enunciado: CERO variables hard-coded, todo parametrizado y
// con programacion defensiva (nada de crashear con entrada mala).
// -----------------------------------------------------------------------------
#pragma once
#include <string>
#include <cstdint>

struct Parametros {
    int    n_particulas = 20000;   // -n --particles   [1, 5,000,000]
    int    ancho        = 800;     // -w --width       [>= 640]
    int    alto         = 600;     // -h --height      [>= 480]
    int    hilos        = 0;       // -t --threads     0 = automatico (omp_get_max_threads)
    uint32_t semilla    = 42;      // -s --seed
    double energia_kt   = 15.0;    // -e --energy      kilotones, escala R(t)
    long   frames_max   = 0;       // -f --frames      0 = infinito (modo interactivo)
    bool   sin_resplandor = false; // --no-glow
    bool   mostrar_ayuda  = false; // --help
};

// Intenta parsear argv en `out`. Si algo es invalido, imprime el error a
// stderr, imprime la ayuda, y devuelve false (el llamador debe salir con
// codigo != 0, sin llegar a inicializar SDL ni asignar memoria).
bool parsear_argumentos(int argc, char** argv, Parametros& out);

void imprimir_ayuda(const char* nombre_programa);
