// main.cpp -- Screensaver de N cuerpos con gravedad. Bucle principal y ventana.
//
// SDL_MAIN_HANDLED: le dice a SDL que NO renombre nuestro main(). Sin esto,
// MinGW intenta enlazar SDL2main y falla con "undefined reference to SDL_main".
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <new>
#include <vector>

#include "cli.h"
#include "nbody.h"
#include "render.h"

namespace {

constexpr uint32_t COLOR_FONDO   = 0xFF05060Eu; // azul casi negro
constexpr uint32_t FACTOR_ESTELA = 175;         // 175/256 ~ 0.68 por frame

// Recursos de SDL agrupados para que la limpieza sea un solo camino de salida.
struct ContextoSDL {
    SDL_Window*   ventana  = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  textura  = nullptr;

    ~ContextoSDL() {
        if (textura)  SDL_DestroyTexture(textura);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (ventana)  SDL_DestroyWindow(ventana);
        SDL_Quit();
    }
};

// Inicializa ventana, renderer y textura. Devuelve false tras imprimir el
// motivo; el destructor de ContextoSDL libera lo que si se alcanzo a crear.
bool iniciar_sdl(ContextoSDL& ctx, const Parametros& p) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "Error: SDL_Init fallo: %s\n", SDL_GetError());
        return false;
    }

    ctx.ventana = SDL_CreateWindow("N-Body",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        p.ancho, p.alto, SDL_WINDOW_SHOWN);
    if (!ctx.ventana) {
        std::fprintf(stderr, "Error: SDL_CreateWindow fallo: %s\n", SDL_GetError());
        return false;
    }

    // Se intenta acelerado por GPU; si el driver no lo soporta se cae a
    // software en vez de abortar. Sin vsync: queremos medir los FPS reales.
    ctx.renderer = SDL_CreateRenderer(ctx.ventana, -1, SDL_RENDERER_ACCELERATED);
    if (!ctx.renderer) {
        std::fprintf(stderr, "Aviso: renderer acelerado no disponible (%s); usando software.\n",
                     SDL_GetError());
        ctx.renderer = SDL_CreateRenderer(ctx.ventana, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ctx.renderer) {
        std::fprintf(stderr, "Error: SDL_CreateRenderer fallo: %s\n", SDL_GetError());
        return false;
    }

    ctx.textura = SDL_CreateTexture(ctx.renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, p.ancho, p.alto);
    if (!ctx.textura) {
        std::fprintf(stderr, "Error: SDL_CreateTexture fallo: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();

    Parametros p;
    if (!parsear_argumentos(argc, argv, p)) {
        std::fprintf(stderr, "\nUse --help para ver las opciones.\n");
        return 1;   // entrada invalida: salir limpio, sin tocar SDL ni reservar memoria
    }
    if (p.mostrar_ayuda) {
        imprimir_ayuda(argv[0]);
        return 0;
    }

    // La memoria se reserva ANTES de abrir la ventana: si N no cabe, es mejor
    // fallar en la consola que dejar una ventana huerfana en pantalla.
    std::unique_ptr<SistemaNCuerpos> sistema;
    std::vector<uint32_t> framebuffer;
    try {
        sistema = std::make_unique<SistemaNCuerpos>(
            p.n_cuerpos, p.ancho, p.alto, p.modo, p.semilla, p.fisica);
        framebuffer.assign(static_cast<size_t>(p.ancho) * p.alto, COLOR_FONDO);
    } catch (const std::bad_alloc&) {
        std::fprintf(stderr,
            "Error: memoria insuficiente para N=%d y canvas %dx%d.\n",
            p.n_cuerpos, p.ancho, p.alto);
        return 1;
    }

    ContextoSDL ctx;
    if (!iniciar_sdl(ctx, p)) return 1;

    std::printf("N=%d  modo=%s  canvas=%dx%d  G=%g  eps=%g  dt=%g\n",
                p.n_cuerpos, texto_de_modo(p.modo), p.ancho, p.alto,
                p.fisica.gravedad, p.fisica.softening, p.fisica.paso);

    bool corriendo = true, pausado = false;
    long frame = 0;
    uint32_t semilla_actual = p.semilla;
    SDL_Event evento;

    auto t_ventana = std::chrono::steady_clock::now();
    int  frames_en_ventana = 0;
    double fps = 0.0;

    while (corriendo) {
        while (SDL_PollEvent(&evento)) {
            if (evento.type == SDL_QUIT) corriendo = false;
            if (evento.type != SDL_KEYDOWN) continue;
            switch (evento.key.keysym.sym) {
                case SDLK_ESCAPE: corriendo = false; break;
                case SDLK_SPACE:  pausado = !pausado; break;
                case SDLK_t:      p.sin_estelas = !p.sin_estelas; break;
                case SDLK_r:
                    semilla_actual = semilla_actual * 1664525u + 1013904223u;
                    sistema->reiniciar(p.modo, semilla_actual);
                    limpiar(framebuffer, COLOR_FONDO);
                    break;
                case SDLK_m:
                    p.modo = static_cast<Modo>((static_cast<int>(p.modo) + 1) % 3);
                    sistema->reiniciar(p.modo, semilla_actual);
                    limpiar(framebuffer, COLOR_FONDO);
                    break;
                default: break;
            }
        }

        // --- Fisica: dos fases separadas por una barrera (ver nbody.h) ---
        if (!pausado) {
            sistema->calcular_aceleraciones();
            sistema->integrar();
        }

        // --- Render ---
        if (p.sin_estelas) limpiar(framebuffer, COLOR_FONDO);
        else               desvanecer(framebuffer, FACTOR_ESTELA);
        dibujar_cuerpos(framebuffer, p.ancho, p.alto, *sistema);

        SDL_UpdateTexture(ctx.textura, nullptr, framebuffer.data(), p.ancho * 4);
        SDL_RenderClear(ctx.renderer);
        SDL_RenderCopy(ctx.renderer, ctx.textura, nullptr, nullptr);
        SDL_RenderPresent(ctx.renderer);

        // --- FPS: promedio sobre ventanas de medio segundo ---
        ++frames_en_ventana;
        const auto ahora = std::chrono::steady_clock::now();
        const double transcurrido = std::chrono::duration<double>(ahora - t_ventana).count();
        if (transcurrido >= 0.5) {
            fps = frames_en_ventana / transcurrido;
            char titulo[160];
            std::snprintf(titulo, sizeof(titulo),
                          "N-Body  |  N=%d  modo=%s  |  FPS=%.1f",
                          p.n_cuerpos, texto_de_modo(p.modo), fps);
            SDL_SetWindowTitle(ctx.ventana, titulo);
            std::printf("FPS= %.2f\n", fps);
            std::fflush(stdout);
            frames_en_ventana = 0;
            t_ventana = ahora;
        }

        if (p.frames_max > 0 && ++frame >= p.frames_max) corriendo = false;
    }

    return 0;   // ContextoSDL libera textura, renderer y ventana al salir de scope
}
