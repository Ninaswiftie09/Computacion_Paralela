// main.cpp -- Screensaver de N cuerpos con gravedad. Bucle principal y ventana.
//
// SDL_MAIN_HANDLED: le dice a SDL que NO renombre nuestro main(). Sin esto,
// MinGW intenta enlazar SDL2main y falla con "undefined reference to SDL_main".
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <new>
#include <vector>

#include "cli.h"
#include "hud.h"
#include "nbody.h"
#include "render.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

constexpr uint32_t COLOR_FONDO   = 0xFF05060Eu; // azul casi negro
constexpr uint32_t FACTOR_ESTELA = 175;         // 175/256 ~ 0.68 por frame

// La energia potencial es O(N^2): recalcularla cada frame duplicaria el costo
// del programa, asi que se refresca cada tantos frames.
constexpr long FRAMES_ENTRE_ENERGIAS = 30;

// Cuantos hilos usa OpenMP ahora mismo (1 si se compilo sin OpenMP).
int hilos_activos() {
#ifdef _OPENMP
    return omp_get_max_threads();
#else
    return 1;
#endif
}

// Fija el numero de hilos. `n` <= 0 significa "los que decida OpenMP".
void fijar_hilos(int n) {
#ifdef _OPENMP
    if (n > 0) omp_set_num_threads(n);
#else
    (void)n;
#endif
}

// Recursos de SDL agrupados para que la limpieza sea un solo camino de salida.
//
// Auditoria de inicializacion/destruccion (Bloque 4): los tres punteros
// arrancan en nullptr, cada uno se pone SOLO si su Create* respectivo tuvo
// exito, y el destructor comprueba cada uno antes de liberarlo. Eso cubre los
// tres caminos de salida posibles de iniciar_sdl(): exito total, fallo a
// mitad de camino (ventana creada pero textura no, por ejemplo), y el
// "camino cero" en que main() nunca llega a construir ContextoSDL porque
// el bad_alloc de sistema/framebuffer se atrapa antes de tocar SDL.
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

// Aplica la politica de reparto elegida en --schedule al schedule(runtime)
// del bucle de fuerzas.
void aplicar_reparto(Reparto r, int trozo) {
#ifdef _OPENMP
    omp_sched_t clase = omp_sched_static;
    switch (r) {
        case Reparto::Estatico: clase = omp_sched_static;  break;
        case Reparto::Dinamico: clase = omp_sched_dynamic; break;
        case Reparto::Guiado:   clase = omp_sched_guided;  break;
    }
    omp_set_schedule(clase, trozo);   // trozo 0 = que OpenMP elija
#else
    (void)r; (void)trozo;
#endif
}

// Corre la simulacion SIN abrir ventana y emite una fila CSV con los tiempos.
// Sirve para el barrido de mediciones: no depende de que haya pantalla, ni de
// vsync, ni del costo de presentar la textura.
int ejecutar_benchmark(const Parametros& p) {
    constexpr long FRAMES_CALENTAMIENTO = 10;   // se descartan: cache fria, hilos arrancando
    const long frames = (p.frames_max > 0) ? p.frames_max : 100;

    std::unique_ptr<SistemaNCuerpos> sistema;
    std::vector<uint32_t> framebuffer;
    try {
        sistema = std::make_unique<SistemaNCuerpos>(
            p.n_cuerpos, p.ancho, p.alto, p.modo, p.semilla, p.fisica);
        framebuffer.assign(static_cast<size_t>(p.ancho) * p.alto, COLOR_FONDO);
    } catch (const std::bad_alloc&) {
        std::fprintf(stderr, "Error: memoria insuficiente para N=%d y canvas %dx%d.\n",
                     p.n_cuerpos, p.ancho, p.alto);
        return 1;
    }

    auto un_frame = [&](double& acum_fisica, double& acum_render) {
        const auto t0 = std::chrono::steady_clock::now();
        sistema->calcular_aceleraciones();
        sistema->integrar();
        const auto t1 = std::chrono::steady_clock::now();
        if (p.sin_estelas) limpiar(framebuffer, COLOR_FONDO);
        else               desvanecer(framebuffer, FACTOR_ESTELA);
        dibujar_cuerpos(framebuffer, p.ancho, p.alto, *sistema, sistema->tono_jitter());
        const auto t2 = std::chrono::steady_clock::now();
        acum_fisica += std::chrono::duration<double, std::milli>(t1 - t0).count();
        acum_render += std::chrono::duration<double, std::milli>(t2 - t1).count();
    };

    double descarte_f = 0.0, descarte_r = 0.0;
    for (long k = 0; k < FRAMES_CALENTAMIENTO; ++k) un_frame(descarte_f, descarte_r);

    double ms_fisica = 0.0, ms_render = 0.0;
    for (long k = 0; k < frames; ++k) un_frame(ms_fisica, ms_render);

    ms_fisica /= frames;
    ms_render /= frames;
    const double ms_frame = ms_fisica + ms_render;

    // modo,n,hilos,schedule,chunk,frames,ms_fisica,ms_render,ms_frame,fps
    std::printf("%s,%d,%d,%s,%d,%ld,%.4f,%.4f,%.4f,%.2f\n",
                texto_de_modo(p.modo), p.n_cuerpos, hilos_activos(),
                texto_de_reparto(p.reparto), p.trozo, frames,
                ms_fisica, ms_render, ms_frame,
                (ms_frame > 0.0) ? 1000.0 / ms_frame : 0.0);
    return 0;
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

    fijar_hilos(p.hilos);
    aplicar_reparto(p.reparto, p.trozo);

    // --bench no necesita ventana: se mide y se sale, sin inicializar SDL.
    if (p.benchmark) return ejecutar_benchmark(p);

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

#ifdef _OPENMP
    const bool con_openmp = true;
#else
    const bool con_openmp = false;
#endif
    // Promedios moviles de los dos cronometros, para que el HUD no parpadee.
    double ms_fisica = 0.0, ms_render = 0.0;

    // Energia inicial: la referencia contra la que se mide el drift. Que se
    // mantenga cerca de 0% es la prueba de que la simulacion es correcta, y de
    // que la version paralela calcula exactamente lo mismo que la secuencial.
    double energia_inicial = sistema->energia_cinetica() + sistema->energia_potencial();
    double energia_actual  = energia_inicial;

    std::printf("N=%d  modo=%s  canvas=%dx%d  G=%g  eps=%g  dt=%g\n",
                p.n_cuerpos, texto_de_modo(p.modo), p.ancho, p.alto,
                p.fisica.gravedad, p.fisica.softening, p.fisica.paso);
    std::printf("hilos=%d  (%s)\n", hilos_activos(),
                con_openmp ? "OpenMP" : "secuencial");

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
                    energia_inicial = sistema->energia_cinetica() +
                                      sistema->energia_potencial();
                    energia_actual  = energia_inicial;
                    break;
                case SDLK_m:
                    p.modo = static_cast<Modo>((static_cast<int>(p.modo) + 1) % 3);
                    sistema->reiniciar(p.modo, semilla_actual);
                    limpiar(framebuffer, COLOR_FONDO);
                    energia_inicial = sistema->energia_cinetica() +
                                      sistema->energia_potencial();
                    energia_actual  = energia_inicial;
                    break;
                default:
                    // 1..9 cambian los hilos EN VIVO: es la demo del speedup.
                    if (evento.key.keysym.sym >= SDLK_1 && evento.key.keysym.sym <= SDLK_9) {
                        fijar_hilos(evento.key.keysym.sym - SDLK_0);
                    }
                    break;
            }
        }

        // --- Fisica: dos fases separadas por una barrera (ver nbody.h) ---
        const auto t0 = std::chrono::steady_clock::now();
        if (!pausado) {
            sistema->calcular_aceleraciones();
            sistema->integrar();
        }
        const auto t1 = std::chrono::steady_clock::now();

        // --- Render ---
        if (p.sin_estelas) limpiar(framebuffer, COLOR_FONDO);
        else               desvanecer(framebuffer, FACTOR_ESTELA);
        dibujar_cuerpos(framebuffer, p.ancho, p.alto, *sistema, sistema->tono_jitter());
        const auto t2 = std::chrono::steady_clock::now();

        // Promedio movil exponencial: suaviza el numero sin guardar historial.
        constexpr double SUAVIZADO = 0.1;
        ms_fisica += SUAVIZADO * (std::chrono::duration<double, std::milli>(t1 - t0).count() - ms_fisica);
        ms_render += SUAVIZADO * (std::chrono::duration<double, std::milli>(t2 - t1).count() - ms_render);

        if (frame % FRAMES_ENTRE_ENERGIAS == 0) {
            energia_actual = sistema->energia_cinetica() + sistema->energia_potencial();
        }

        MetricasHud met;
        met.n_cuerpos = p.n_cuerpos;
        met.hilos     = hilos_activos();
        met.fps       = fps;
        met.ms_fisica = ms_fisica;
        met.ms_render = ms_render;
        met.energia   = energia_actual;
        met.drift_pct = 100.0 * (energia_actual - energia_inicial) / std::fabs(energia_inicial);
        met.modo      = texto_de_modo(p.modo);
        met.pausado   = pausado;
        met.paralelo  = con_openmp;
        dibujar_hud(framebuffer, p.ancho, p.alto, met);

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
