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
        "  -e, --softening E   Suavizado en px, evita r=0   (def. 15, rango (0.001, 10000])\n"
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
            // El piso NO es 0.0 por una razon de punto flotante, no solo fisica:
            // eps^2 se guarda en float, y con softening por debajo de ~1e-19 ese
            // cuadrado hace underflow a exactamente 0.0f. Cuando eso pasa, el
            // termino j==i del bucle de fuerzas (donde dx=dy=0 siempre) calcula
            // 1/sqrt(0) = inf y luego 0*inf = NaN, que contamina TODO el sistema
            // en un solo paso -- verificado con -e 1e-25, que sin este piso caia
            // dentro del rango permitido. 1e-3 deja variar el softening en mas de
            // 6 ordenes de magnitud por debajo del default (15) sin acercarse al
            // underflow (eps^2 = 1e-6, muy por encima del minimo normal de float).
            // El limite inferior real es 1e-3; se pasa apenas por debajo para
            // que escribir exactamente "0.001" (el minimo documentado) no caiga
            // del lado excluido del rango abierto que usa leer_real().
            if (!leer_real(argc, argv, i, arg, 1e-3 - 1e-9, 1e4, real)) return false;
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

    // --- Defensive programming: cross-validation of edge cases ---
    // Edge case 1: Very large dt combined with large G can lead to explosive integration.
    // Sanity check: G * dt should stay below a reasonable threshold to avoid NaN.
    const double G_dt_product = out.fisica.gravedad * out.fisica.paso;
    if (G_dt_product > 100.0) {
        std::fprintf(stderr,
            "Aviso: --gravity * --dt = %.2f es muy grande; "
            "la integracion puede volverse inestable o producir NaN.\n"
            "       Considere reducir --gravity o --dt.\n", G_dt_product);
    }

    // Edge case 2: Very small softening risks numerical issues even with float safeguards.
    if (out.fisica.softening < 0.01) {
        std::fprintf(stderr,
            "Aviso: --softening = %.2e es muy pequeno; "
            "el suavizado de Plummer puede ser inefectivo.\n", out.fisica.softening);
    }

    // Edge case 3: Extremely large canvas can exhaust memory even with reasonable N.
    const long pixel_total = static_cast<long>(out.ancho) * static_cast<long>(out.alto);
    if (pixel_total > 16000000L) {  // >4K (3840x4320)
        std::fprintf(stderr,
            "Aviso: canvas de %dx%d = %.1f megapixeles; "
            "la GPU puede ser lenta o la memoria insuficiente.\n",
            out.ancho, out.alto, pixel_total / 1e6);
    }

    // Edge case 4: N=2 or N=3 are extreme: collision distributes 1 body per galaxy.
    if (out.n_cuerpos == 2) {
        std::fprintf(stderr,
            "Aviso: N=2 es un caso extremo; cada cuerpo es un nucleo sin estrellas.\n");
    }
    if (out.n_cuerpos == 3) {
        std::fprintf(stderr,
            "Aviso: N=3 es un caso extremo en modo colision; "
            "se distribuyen 1-2 cuerpos por galaxia.\n");
    }

    return true;
}
