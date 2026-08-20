// main.cpp
// -----------------------------------------------------------------------------
// Screensaver "Detonacion" -- version SECUENCIAL.
// Proyecto #1, Computacion Paralela y Distribuida (UVG, Seccion 20).
//
// Ciclo de vida por frame:
//   1. actualizar()          -- fisica de las N particulas (dt fijo)
//   2. limpiar framebuffer
//   3. acumular_resplandor() -- campo de brillo por pixel
//   4. subir framebuffer a una textura SDL y presentar
//   5. medir/mostrar FPS
//
// Al terminar un ciclo (t > DURACION_CICLO), la escena se reinicia con una
// semilla derivada, para que nunca se vea exactamente igual dos veces.
// -----------------------------------------------------------------------------
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include <chrono>

#include "cli.h"
#include "particles.h"

namespace {
constexpr float  DT_FIJO = 1.0f / 60.0f;   // paso de simulacion fijo, independiente del render
constexpr double DURACION_CICLO = 15.0;    // segundos; ver docs/PROPUESTA.md tabla de fases

// Limpia el framebuffer a un color de cielo nocturno oscuro.
void limpiar_framebuffer(std::vector<uint8_t>& fb, int /*ancho*/, int /*alto*/) {
    for (size_t i = 0; i < fb.size(); i += 4) {
        fb[i + 0] = 8;   // R
        fb[i + 1] = 8;   // G
        fb[i + 2] = 14;  // B
        fb[i + 3] = 255; // A
    }
}

// Destello: en la fase inicial se lava la pantalla a blanco con caida
// exponencial de luminancia (ver docs/PROPUESTA.md, fase 1).
void aplicar_destello(std::vector<uint8_t>& fb, double t_fase) {
    float intensidad = static_cast<float>(std::exp(-t_fase / 0.08));
    if (intensidad < 0.02f) return;
    for (size_t i = 0; i < fb.size(); i += 4) {
        fb[i + 0] = static_cast<uint8_t>(fb[i + 0] + (255 - fb[i + 0]) * intensidad);
        fb[i + 1] = static_cast<uint8_t>(fb[i + 1] + (255 - fb[i + 1]) * intensidad);
        fb[i + 2] = static_cast<uint8_t>(fb[i + 2] + (255 - fb[i + 2]) * intensidad);
    }
}

} // namespace

int main(int argc, char** argv) {
    Parametros p;
    if (!parsear_argumentos(argc, argv, p)) {
        std::fprintf(stderr, "\n");
        imprimir_ayuda(argv[0]);
        return 1; // programacion defensiva: entrada invalida -> salir limpio, sin tocar SDL
    }
    if (p.mostrar_ayuda) {
        imprimir_ayuda(argv[0]);
        return 0;
    }

    // --- Nota sobre --threads en la version secuencial ---
    // Este binario no usa OpenMP; el flag se acepta y valida por compatibilidad
    // de linea de comandos con la version paralela (mismo parser, mismo CLI),
    // pero no tiene efecto aqui.
    if (p.hilos != 0) {
        std::fprintf(stderr,
            "Aviso: --threads no tiene efecto en la version secuencial (se ignora).\n");
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "Error: SDL_Init fallo: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* ventana = SDL_CreateWindow(
        "Detonacion -- screensaver secuencial",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        p.ancho, p.alto, SDL_WINDOW_SHOWN);
    if (!ventana) {
        std::fprintf(stderr, "Error: SDL_CreateWindow fallo: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Se intenta primero un renderer acelerado con vsync; si el driver de
    // video no lo soporta (ej. entornos headless / CI), se cae a software.
    // Programacion defensiva: nunca abortar solo porque no hay GPU.
    SDL_Renderer* renderer = SDL_CreateRenderer(
        ventana, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::fprintf(stderr,
            "Aviso: renderer acelerado no disponible (%s); usando software.\n",
            SDL_GetError());
        renderer = SDL_CreateRenderer(ventana, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        std::fprintf(stderr, "Error: SDL_CreateRenderer fallo: %s\n", SDL_GetError());
        SDL_DestroyWindow(ventana);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* textura = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
        p.ancho, p.alto);
    if (!textura) {
        std::fprintf(stderr, "Error: SDL_CreateTexture fallo: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(ventana);
        SDL_Quit();
        return 1;
    }

    std::vector<uint8_t> framebuffer(static_cast<size_t>(p.ancho) * p.alto * 4);

    // Programacion defensiva: si malloc/vector fallara por N o resolucion
    // absurdamente grandes, std::bad_alloc se lanzaria; lo atrapamos simple:
    if (framebuffer.empty() && p.ancho > 0 && p.alto > 0) {
        std::fprintf(stderr, "Error: no se pudo reservar el framebuffer.\n");
        return 1;
    }

    auto crear_escena_y_particulas = [&](uint32_t semilla)
        -> std::pair<std::unique_ptr<Escena>, std::unique_ptr<SistemaParticulas>> {
        auto escena = std::make_unique<Escena>(p.ancho, p.alto, p.energia_kt, semilla);
        auto sistema = std::make_unique<SistemaParticulas>(p.n_particulas, *escena);
        return {std::move(escena), std::move(sistema)};
    };

    uint32_t semilla_actual = p.semilla;
    auto [escena, particulas] = crear_escena_y_particulas(semilla_actual);

    bool corriendo = true;
    long frame_num = 0;
    SDL_Event evento;

    // --- Medicion de FPS (promedio movil simple sobre 1 segundo) ---
    auto t_prev_fps = std::chrono::steady_clock::now();
    int frames_desde_medicion = 0;
    double fps_actual = 0.0;

    while (corriendo) {
        while (SDL_PollEvent(&evento)) {
            if (evento.type == SDL_QUIT) corriendo = false;
            if (evento.type == SDL_KEYDOWN && evento.key.keysym.sym == SDLK_ESCAPE) {
                corriendo = false;
            }
        }

        // 1. Fisica
        particulas->actualizar(*escena, DT_FIJO);

        // Reinicio de ciclo: nueva semilla derivada, nunca se ve igual dos veces.
        if (escena->t > DURACION_CICLO) {
            semilla_actual = semilla_actual * 1664525u + 1013904223u; // LCG simple
            auto nuevo = crear_escena_y_particulas(semilla_actual);
            escena = std::move(nuevo.first);
            particulas = std::move(nuevo.second);
        }

        // 2. Limpiar + destello si aplica
        limpiar_framebuffer(framebuffer, p.ancho, p.alto);
        if (escena->fase_actual() == Fase::Destello) {
            aplicar_destello(framebuffer, escena->t);
        }

        // 3. Campo de resplandor
        if (!p.sin_resplandor) {
            acumular_resplandor(framebuffer.data(), p.ancho, p.alto, *particulas);
        }

        // 4. Presentar
        SDL_UpdateTexture(textura, nullptr, framebuffer.data(), p.ancho * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, textura, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        // 5. FPS
        ++frames_desde_medicion;
        auto ahora = std::chrono::steady_clock::now();
        double transcurrido = std::chrono::duration<double>(ahora - t_prev_fps).count();
        if (transcurrido >= 1.0) {
            fps_actual = frames_desde_medicion / transcurrido;
            char titulo[128];
            std::snprintf(titulo, sizeof(titulo),
                "Detonacion -- N=%d  FPS=%.2f", p.n_particulas, fps_actual);
            SDL_SetWindowTitle(ventana, titulo);
            std::printf("FPS= %.2f\n", fps_actual);
            std::fflush(stdout);
            frames_desde_medicion = 0;
            t_prev_fps = ahora;
        }

        ++frame_num;
        if (p.frames_max > 0 && frame_num >= p.frames_max) {
            corriendo = false; // modo benchmark: correr F frames y salir
        }
    }

    SDL_DestroyTexture(textura);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(ventana);
    SDL_Quit();
    return 0;
}
