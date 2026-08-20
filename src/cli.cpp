// cli.cpp
#include "cli.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>

namespace {

// Convierte `texto` a long, validando que sea numerico completo.
// Devuelve false si hay basura, esta vacio, o hay overflow.
bool a_long(const std::string& texto, long& out) {
    if (texto.empty()) return false;
    errno = 0;
    char* fin = nullptr;
    long v = std::strtol(texto.c_str(), &fin, 10);
    if (errno == ERANGE) return false;
    if (fin == texto.c_str() || *fin != '\0') return false; // basura al final
    out = v;
    return true;
}

bool a_double(const std::string& texto, double& out) {
    if (texto.empty()) return false;
    errno = 0;
    char* fin = nullptr;
    double v = std::strtod(texto.c_str(), &fin);
    if (errno == ERANGE) return false;
    if (fin == texto.c_str() || *fin != '\0') return false;
    out = v;
    return true;
}

// Devuelve el siguiente argv como string, o false si no hay valor
// despues de una bandera que lo requiere (ej. "-n" al final de la linea).
bool siguiente_valor(int argc, char** argv, int& i, std::string& out) {
    if (i + 1 >= argc) return false;
    out = argv[++i];
    return true;
}

} // namespace

void imprimir_ayuda(const char* nombre_programa) {
    std::printf(
        "Uso: %s [opciones]\n"
        "\n"
        "Screensaver \"Detonacion\" -- Proyecto 1, Computacion Paralela y Distribuida\n"
        "\n"
        "Opciones:\n"
        "  -n, --particles N     Numero de particulas          (default 20000, rango 1..5000000)\n"
        "  -w, --width W         Ancho del canvas               (default 800,  minimo 640)\n"
        "  -h, --height H        Alto del canvas                (default 600,  minimo 480)\n"
        "  -t, --threads T       Hilos OpenMP                   (default: automatico)\n"
        "  -s, --seed S          Semilla del RNG                (default 42)\n"
        "  -e, --energy E        Energia en kilotones           (default 15.0)\n"
        "  -f, --frames F        Correr F frames y salir        (default 0 = infinito)\n"
        "      --no-glow         Desactiva el campo de resplandor por pixel\n"
        "      --help            Muestra esta ayuda\n",
        nombre_programa);
}

bool parsear_argumentos(int argc, char** argv, Parametros& out) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            out.mostrar_ayuda = true;
            return true; // no es un error, el llamador debe imprimir ayuda y salir con 0
        }

        if (arg == "-n" || arg == "--particles") {
            std::string val;
            long v;
            if (!siguiente_valor(argc, argv, i, val) || !a_long(val, v)) {
                std::fprintf(stderr, "Error: %s requiere un entero valido.\n", arg.c_str());
                return false;
            }
            if (v < 1 || v > 5000000) {
                std::fprintf(stderr, "Error: --particles debe estar en [1, 5000000] (recibido %ld).\n", v);
                return false;
            }
            out.n_particulas = static_cast<int>(v);
            continue;
        }

        if (arg == "-w" || arg == "--width") {
            std::string val; long v;
            if (!siguiente_valor(argc, argv, i, val) || !a_long(val, v)) {
                std::fprintf(stderr, "Error: %s requiere un entero valido.\n", arg.c_str());
                return false;
            }
            if (v < 640) {
                std::fprintf(stderr, "Error: --width debe ser >= 640 (recibido %ld).\n", v);
                return false;
            }
            out.ancho = static_cast<int>(v);
            continue;
        }

        if (arg == "-h" || arg == "--height") {
            std::string val; long v;
            if (!siguiente_valor(argc, argv, i, val) || !a_long(val, v)) {
                std::fprintf(stderr, "Error: %s requiere un entero valido.\n", arg.c_str());
                return false;
            }
            if (v < 480) {
                std::fprintf(stderr, "Error: --height debe ser >= 480 (recibido %ld).\n", v);
                return false;
            }
            out.alto = static_cast<int>(v);
            continue;
        }

        if (arg == "-t" || arg == "--threads") {
            std::string val; long v;
            if (!siguiente_valor(argc, argv, i, val) || !a_long(val, v)) {
                std::fprintf(stderr, "Error: %s requiere un entero valido.\n", arg.c_str());
                return false;
            }
            if (v < 0 || v > 1024) {
                std::fprintf(stderr, "Error: --threads fuera de rango razonable (recibido %ld).\n", v);
                return false;
            }
            out.hilos = static_cast<int>(v);
            continue;
        }

        if (arg == "-s" || arg == "--seed") {
            std::string val; long v;
            if (!siguiente_valor(argc, argv, i, val) || !a_long(val, v)) {
                std::fprintf(stderr, "Error: %s requiere un entero valido.\n", arg.c_str());
                return false;
            }
            out.semilla = static_cast<uint32_t>(v);
            continue;
        }

        if (arg == "-e" || arg == "--energy") {
            std::string val; double v;
            if (!siguiente_valor(argc, argv, i, val) || !a_double(val, v)) {
                std::fprintf(stderr, "Error: %s requiere un numero valido.\n", arg.c_str());
                return false;
            }
            if (v <= 0.0 || v > 100000.0) {
                std::fprintf(stderr, "Error: --energy debe estar en (0, 100000] kt (recibido %g).\n", v);
                return false;
            }
            out.energia_kt = v;
            continue;
        }

        if (arg == "-f" || arg == "--frames") {
            std::string val; long v;
            if (!siguiente_valor(argc, argv, i, val) || !a_long(val, v)) {
                std::fprintf(stderr, "Error: %s requiere un entero valido.\n", arg.c_str());
                return false;
            }
            if (v < 0) {
                std::fprintf(stderr, "Error: --frames no puede ser negativo (recibido %ld).\n", v);
                return false;
            }
            out.frames_max = v;
            continue;
        }

        if (arg == "--no-glow") {
            out.sin_resplandor = true;
            continue;
        }

        std::fprintf(stderr, "Error: argumento desconocido '%s'.\n", arg.c_str());
        return false;
    }

    return true;
}
