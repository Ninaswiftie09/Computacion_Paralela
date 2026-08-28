#include "cli.h"
#include <cstdio>
#include <cstdlib>
#include <cerrno>

namespace {

// Convierte `texto` a long validando que sea numerico COMPLETO.
// Devuelve false si esta vacio, si sobra basura al final, o si hay overflow.
bool a_long(const std::string& texto, long& out) {
    if (texto.empty()) return false;
    errno = 0;
    char* fin = nullptr;
    long v = std::strtol(texto.c_str(), &fin, 10);
    if (errno == ERANGE) return false;
    if (fin == texto.c_str() || *fin != '\0') return false;
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

// Toma el siguiente argv como valor de la bandera actual. Devuelve false si la
// bandera venia al final de la linea sin valor (ej: "... -n").
bool siguiente_valor(int argc, char** argv, int& i, std::string& out) {
    if (i + 1 >= argc) return false;
    out = argv[++i];
    return true;
}

// Lee un entero de la siguiente posicion y verifica que caiga en [lo, hi].
bool leer_entero(int argc, char** argv, int& i, const std::string& bandera,
                 long lo, long hi, long& out) {
    std::string val;
    if (!siguiente_valor(argc, argv, i, val) || !a_long(val, out)) {
        std::fprintf(stderr, "Error: %s requiere un entero valido.\n", bandera.c_str());
        return false;
    }
    if (out < lo || out > hi) {
        std::fprintf(stderr, "Error: %s debe estar en [%ld, %ld] (recibido %ld).\n",
                     bandera.c_str(), lo, hi, out);
        return false;
    }
    return true;
}

// Igual que leer_entero pero para reales, con rango abierto por abajo (lo, hi].
bool leer_real(int argc, char** argv, int& i, const std::string& bandera,
               double lo, double hi, double& out) {
    std::string val;
    if (!siguiente_valor(argc, argv, i, val) || !a_double(val, out)) {
        std::fprintf(stderr, "Error: %s requiere un numero valido.\n", bandera.c_str());
        return false;
    }
    if (!(out > lo) || out > hi) {
        std::fprintf(stderr, "Error: %s debe estar en (%g, %g] (recibido %g).\n",
                     bandera.c_str(), lo, hi, out);
        return false;
    }
    return true;
}

} // namespace

bool reparto_desde_texto(const std::string& texto, Reparto& out) {
    if (texto == "static")  { out = Reparto::Estatico; return true; }
    if (texto == "dynamic") { out = Reparto::Dinamico; return true; }
    if (texto == "guided")  { out = Reparto::Guiado;   return true; }
    return false;
}

const char* texto_de_reparto(Reparto r) {
    switch (r) {
        case Reparto::Estatico: return "static";
        case Reparto::Dinamico: return "dynamic";
        case Reparto::Guiado:   return "guided";
    }
    return "?";
}

void imprimir_ayuda(const char* nombre_programa) {
    std::printf(
        "Uso: %s [opciones]\n"
        "\n"
        "Screensaver de N cuerpos con gravedad newtoniana.\n"
        "Proyecto #1 -- Computacion Paralela y Distribuida (UVG, Seccion 20).\n"
        "\n"
        "Simulacion:\n"
        "  -n, --particles N   Numero de cuerpos            (def. 5000, rango 2..200000)\n"
        "  -m, --mode MODO     colision | galaxia | nube    (def. colision)\n"
        "  -s, --seed S        Semilla del generador        (def. 42)\n"
        "\n"
        "Fisica:\n"
        "  -G, --gravity G     Constante gravitacional      (def. 100)\n"
        "  -e, --softening E   Suavizado en px, evita r=0   (def. 15)\n"
        "  -d, --dt DT         Paso de tiempo en segundos   (def. 0.020)\n"
        "\n"
        "Ventana y ejecucion:\n"
        "  -w, --width W       Ancho del canvas             (def. 1000, minimo 640)\n"
        "  -h, --height H      Alto del canvas              (def. 700,  minimo 480)\n"
        "  -t, --threads T     Hilos OpenMP, 0 = automatico (def. 0)\n"
        "  -f, --frames F      Correr F frames y salir      (def. 0 = infinito)\n"
        "      --no-trails     Borra la pantalla en vez de dejar estelas\n"
        "      --help          Muestra esta ayuda\n"
        "\n"
        "Teclas durante la ejecucion:\n"
        "  1-9   cambia el numero de hilos en vivo      ESPACIO  pausa\n"
        "  R     reinicia con otra semilla              T        alterna estelas\n"
        "  M     cambia de escenario                    ESC      salir\n"
        "\n"
        "Ejemplos:\n"
        "  %s -n 8000 -m colision -t 8\n"
        "  %s -n 2000 -m galaxia --no-trails\n",
        nombre_programa, nombre_programa, nombre_programa);
}

bool parsear_argumentos(int argc, char** argv, Parametros& out) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        long entero = 0;
        double real = 0.0;

        if (arg == "--help") {
            out.mostrar_ayuda = true;
            return true;   // no es error: el llamador imprime la ayuda y sale con 0
        }

        if (arg == "-n" || arg == "--particles") {
            // El minimo es 2: con un solo cuerpo no hay interaccion que simular.
            if (!leer_entero(argc, argv, i, arg, 2, 200000, entero)) return false;
            out.n_cuerpos = static_cast<int>(entero);
            continue;
        }
        if (arg == "-w" || arg == "--width") {
            if (!leer_entero(argc, argv, i, arg, 640, 7680, entero)) return false;
            out.ancho = static_cast<int>(entero);
            continue;
        }
        if (arg == "-h" || arg == "--height") {
            if (!leer_entero(argc, argv, i, arg, 480, 4320, entero)) return false;
            out.alto = static_cast<int>(entero);
            continue;
        }
        if (arg == "-t" || arg == "--threads") {
            if (!leer_entero(argc, argv, i, arg, 0, 1024, entero)) return false;
            out.hilos = static_cast<int>(entero);
            continue;
        }
        if (arg == "-f" || arg == "--frames") {
            if (!leer_entero(argc, argv, i, arg, 0, 100000000L, entero)) return false;
            out.frames_max = entero;
            continue;
        }
        if (arg == "-s" || arg == "--seed") {
            if (!leer_entero(argc, argv, i, arg, 0, 4294967295L, entero)) return false;
            out.semilla = static_cast<uint32_t>(entero);
            continue;
        }

        if (arg == "-G" || arg == "--gravity") {
            if (!leer_real(argc, argv, i, arg, 0.0, 1e6, real)) return false;
            out.fisica.gravedad = real;
            continue;
        }
        if (arg == "-e" || arg == "--softening") {
            // Debe ser > 0: con eps = 0 la fuerza diverge cuando dos cuerpos
            // coinciden y la simulacion explota a NaN.
            if (!leer_real(argc, argv, i, arg, 0.0, 1e4, real)) return false;
            out.fisica.softening = real;
            continue;
        }
        if (arg == "-d" || arg == "--dt") {
            if (!leer_real(argc, argv, i, arg, 0.0, 1.0, real)) return false;
            out.fisica.paso = real;
            continue;
        }

        if (arg == "-m" || arg == "--mode") {
            std::string val;
            if (!siguiente_valor(argc, argv, i, val) || !modo_desde_texto(val, out.modo)) {
                std::fprintf(stderr,
                    "Error: %s debe ser colision, galaxia o nube.\n", arg.c_str());
                return false;
            }
            continue;
        }

        if (arg == "--no-trails") { out.sin_estelas = true; continue; }
        if (arg == "--bench")     { out.benchmark   = true; continue; }

        if (arg == "--schedule") {
            std::string val;
            if (!siguiente_valor(argc, argv, i, val) || !reparto_desde_texto(val, out.reparto)) {
                std::fprintf(stderr,
                    "Error: --schedule debe ser static, dynamic o guided.\n");
                return false;
            }
            continue;
        }
        if (arg == "--chunk") {
            if (!leer_entero(argc, argv, i, arg, 0, 1000000, entero)) return false;
            out.trozo = static_cast<int>(entero);
            continue;
        }

        std::fprintf(stderr, "Error: argumento desconocido '%s'.\n", arg.c_str());
        return false;
    }

    return true;
}
