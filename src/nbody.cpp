#include "nbody.h"
#include <cmath>
#include <algorithm>
#ifdef _OPENMP
#include <omp.h>
#endif

namespace {
constexpr float PI = 3.14159265358979323846f;

// --- Constantes de escena (no son la fisica, solo como se siembra) ---
// La masa TOTAL de cada galaxia es fija e independiente de N: si N cambia, cada
// estrella pesa menos y la dinamica se ve igual. Eso hace que el benchmark
// compare peras con peras al barrer N.
constexpr float MASA_GALAXIA    = 6000.0f;
constexpr float FRACCION_NUCLEO = 0.85f;  // el nucleo domina: orbitas limpias y estables
constexpr float RADIO_DISCO_REL = 0.30f;  // radio del disco / min(ancho, alto)
constexpr float RADIO_MIN_REL   = 0.05f;  // agujero central: nada nace sobre el nucleo
constexpr float VEL_ACERCAMIENTO = 28.0f; // px/s con que se embisten las galaxias

} // namespace

bool modo_desde_texto(const std::string& texto, Modo& out) {
    if (texto == "colision") { out = Modo::Colision; return true; }
    if (texto == "galaxia")  { out = Modo::Galaxia;  return true; }
    if (texto == "nube")     { out = Modo::Nube;     return true; }
    return false;
}

const char* texto_de_modo(Modo m) {
    switch (m) {
        case Modo::Colision: return "colision";
        case Modo::Galaxia:  return "galaxia";
        case Modo::Nube:     return "nube";
    }
    return "?";
}

SistemaNCuerpos::SistemaNCuerpos(int n, int ancho, int alto, Modo modo,
                                 uint32_t semilla, const ParametrosFisica& fisica)
    : n_(n), ancho_(ancho), alto_(alto), fisica_(fisica) {
    // resize() lanza std::bad_alloc si N es absurdo; main.cpp lo atrapa.
    px_.resize(n); py_.resize(n);
    vx_.resize(n); vy_.resize(n);
    ax_.resize(n); ay_.resize(n);
    masa_.resize(n);
    reiniciar(modo, semilla);
}

void SistemaNCuerpos::sembrar_disco(Xorshift32& r, int desde, int cuenta,
                                    float cx, float cy,
                                    float vx_bulk, float vy_bulk, int giro) {
    if (cuenta <= 0) return;

    const float lado    = static_cast<float>(std::min(ancho_, alto_));
    const float radio   = RADIO_DISCO_REL * lado;
    const float r_min   = RADIO_MIN_REL   * lado;
    const float m_nucleo = MASA_GALAXIA * FRACCION_NUCLEO;
    const float m_disco  = MASA_GALAXIA - m_nucleo;
    const int   estrellas = cuenta - 1;
    const float G = static_cast<float>(fisica_.gravedad);

    // Cuerpo 0 de la galaxia: el nucleo, quieto respecto a su propia galaxia.
    px_[desde] = cx;  py_[desde] = cy;
    vx_[desde] = vx_bulk;  vy_[desde] = vy_bulk;
    masa_[desde] = m_nucleo;

    if (estrellas <= 0) return;
    const float m_estrella = m_disco / static_cast<float>(estrellas);

    for (int k = 0; k < estrellas; ++k) {
        const int i = desde + 1 + k;

        // r = r_min + (R - r_min)*u reparte los cuerpos con densidad superficial
        // proporcional a 1/r: concentrados al centro y ralos en el borde, que es
        // como se ve una galaxia de verdad.
        const float ang = r.rango(0.0f, 2.0f * PI);
        const float rad = r_min + (radio - r_min) * r.uniforme01();

        px_[i] = cx + std::cos(ang) * rad;
        py_[i] = cy + std::sin(ang) * rad;
        masa_[i] = m_estrella;

        // Velocidad circular usando la masa ENCERRADA dentro del radio rad
        // (nucleo + la parte del disco que queda adentro): es la curva de
        // rotacion de la galaxia, no solo la atraccion del nucleo.
        // Con densidad ~1/r la masa encerrada crece lineal con el radio.
        const float m_enc = m_nucleo + m_disco * (rad / radio);
        const float v = std::sqrt(G * m_enc / rad);

        // Tangencial = perpendicular al radio. Aqui entra la trigonometria.
        vx_[i] = vx_bulk + giro * (-std::sin(ang)) * v;
        vy_[i] = vy_bulk + giro * ( std::cos(ang)) * v;
    }
}

void SistemaNCuerpos::sembrar_nube(Xorshift32& r) {
    // Sin nucleo ni rotacion: masas iguales, casi en reposo. La gravedad sola
    // las colapsa en filamentos y cumulos.
    const float m = MASA_GALAXIA / static_cast<float>(n_);
    const float mx = ancho_ * 0.15f, my = alto_ * 0.15f;
    for (int i = 0; i < n_; ++i) {
        px_[i] = r.rango(mx, ancho_ - mx);
        py_[i] = r.rango(my, alto_  - my);
        vx_[i] = r.rango(-6.0f, 6.0f);
        vy_[i] = r.rango(-6.0f, 6.0f);
        masa_[i] = m;
    }
}

void SistemaNCuerpos::reiniciar(Modo modo, uint32_t semilla) {
    Xorshift32 r(semilla);
    std::fill(ax_.begin(), ax_.end(), 0.0f);
    std::fill(ay_.begin(), ay_.end(), 0.0f);

    const float w = static_cast<float>(ancho_), h = static_cast<float>(alto_);

    switch (modo) {
        case Modo::Colision: {
            // Dos galaxias desplazadas en diagonal y con velocidades opuestas:
            // no chocan de frente, se rozan -> se forman colas de marea.
            const int n_a = n_ / 2;
            const int n_b = n_ - n_a;
            sembrar_disco(r, 0,   n_a, w * 0.28f, h * 0.30f,
                           VEL_ACERCAMIENTO,  VEL_ACERCAMIENTO * 0.35f, +1);
            sembrar_disco(r, n_a, n_b, w * 0.72f, h * 0.70f,
                          -VEL_ACERCAMIENTO, -VEL_ACERCAMIENTO * 0.35f, -1);
            break;
        }
        case Modo::Galaxia:
            sembrar_disco(r, 0, n_, w * 0.5f, h * 0.5f, 0.0f, 0.0f, +1);
            break;
        case Modo::Nube:
            sembrar_nube(r);
            break;
    }

    // Escala de referencia para el color: la velocidad circular en el borde del
    // disco. Fija y reproducible, asi el color no parpadea entre frames.
    const float lado = static_cast<float>(std::min(ancho_, alto_));
    rapidez_ref_ = std::sqrt(static_cast<float>(fisica_.gravedad) * MASA_GALAXIA /
                             (RADIO_DISCO_REL * lado)) * 2.4f;
    if (rapidez_ref_ < 1.0f) rapidez_ref_ = 1.0f; // nunca dividir entre ~0
}

void SistemaNCuerpos::calcular_aceleraciones() {
    // Piso de defensa en profundidad: cli.cpp ya exige softening >= 1e-3 para
    // que eps^2 no haga underflow a 0.0f, pero esta funcion no deberia depender
    // de quien la llama para ser segura (SistemaNCuerpos tambien se puede usar
    // fuera de la CLI). Si eps^2 llegara a 0, el termino j==i de abajo (donde
    // dx=dy=0 siempre) calcularia 1/sqrt(0)=inf y 0*inf=NaN, contaminando todo
    // el sistema en un solo paso. El piso no cambia nada en el rango normal de
    // uso: 1e-6 en px^2 es invisible frente al softening tipico (15^2 = 225).
    constexpr float EPS2_MINIMO = 1e-6f;
    const float G    = static_cast<float>(fisica_.gravedad);
    const float eps2 = std::max(static_cast<float>(fisica_.softening * fisica_.softening),
                                EPS2_MINIMO);
    const int   n    = n_;

    // Punteros crudos con __restrict izados fuera del bucle. Sin esto el
    // compilador tiene que asumir que los vectores podrian solaparse entre si,
    // y no se atreve a vectorizar el bucle interno.
    const float* __restrict pos_x = px_.data();
    const float* __restrict pos_y = py_.data();
    const float* __restrict masa  = masa_.data();
    float* __restrict acc_x_out = ax_.data();
    float* __restrict acc_y_out = ay_.data();

    // --- El bucle O(N^2): el 95% del tiempo de CPU del programa ---
    // Cada cuerpo i es independiente: escribe solo su ax_[i]/ay_[i] y unicamente
    // LEE las posiciones de los demas. Al no haber escrituras compartidas, no
    // hace falta ningun mecanismo de proteccion aqui.
    //
    // schedule(runtime): la politica se elige con --schedule, para poder medirlas
    // (usar runtime en vez de una politica fija no cuesta nada, medido).
    //
    // La regla de libro dice que aqui deberia ganar static: todo i cuesta
    // exactamente lo mismo (N iteraciones internas), asi que un reparto fijo ya
    // queda balanceado y no paga sincronizacion. Medido, gana dynamic:
    //
    //     hilos    static   dynamic   guided     (ms/paso, N=8000)
    //       8      14.57     13.97     14.13
    //      16      11.63      9.96     10.39
    //      32      10.65      8.60      9.27
    //
    // El motivo es que la regla supone que todos los nucleos son iguales, y en
    // un CPU hibrido no lo son: este i9-13900HX tiene P-cores rapidos y E-cores
    // lentos. Aunque cada hilo reciba la misma CANTIDAD de trabajo, el que cae
    // en un E-core tarda mas y los demas lo esperan en la barrera. Dynamic
    // reparte sobre la marcha: los nucleos rapidos toman mas trozos. Por eso la
    // ventaja crece con el numero de hilos (4% con 8, 19% con 32).
    //
    // Nota medida: con -fopenmp GCC deja de vectorizar el bucle interno
    // ("unsupported control flow in loop", ver -fopt-info-vec), asi que cada
    // hilo corre a ~la mitad de la velocidad del binario secuencial. Se probaron
    // omp simd, extraer el bucle a su propia funcion y bloqueo de registros:
    // las tres empeoraron el tiempo absoluto. Esta forma es la mas rapida de
    // las medidas, tanto en secuencial como en paralelo.
    #pragma omp parallel for schedule(runtime)
    for (int i = 0; i < n; ++i) {
        const float xi = pos_x[i], yi = pos_y[i];
        float acc_x = 0.0f, acc_y = 0.0f;

        for (int j = 0; j < n; ++j) {
            const float dx = pos_x[j] - xi;
            const float dy = pos_y[j] - yi;

            // Softening de Plummer: el eps^2 evita que la fuerza explote cuando
            // dos cuerpos casi se tocan. Ademas hace que el caso j == i aporte
            // exactamente 0, asi que no hace falta un `if (i != j)` que romperia
            // la vectorizacion del bucle interno.
            const float r2     = dx * dx + dy * dy + eps2;
            const float inv_r  = 1.0f / std::sqrt(r2);
            const float inv_r3 = inv_r * inv_r * inv_r;   // 1/r^3, sin pow()

            const float p = masa[j] * inv_r3;
            acc_x += dx * p;
            acc_y += dy * p;
        }

        acc_x_out[i] = G * acc_x;
        acc_y_out[i] = G * acc_y;
    }
    // Barrera implicita al cerrar el parallel for: ningun hilo sigue hasta que
    // TODAS las aceleraciones esten listas. Recien entonces integrar() puede
    // mover los cuerpos sin que nadie lea una posicion a medio actualizar.
}

void SistemaNCuerpos::integrar() {
    const float dt = static_cast<float>(fisica_.paso);

    // Euler semi-implicito: se actualiza v con la aceleracion nueva y LUEGO se
    // mueve p con la v ya actualizada. Conserva la energia mucho mejor que el
    // Euler explicito, con el mismo costo.
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n_; ++i) {
        vx_[i] += ax_[i] * dt;
        vy_[i] += ay_[i] * dt;
        px_[i] += vx_[i] * dt;
        py_[i] += vy_[i] * dt;
    }
}

double SistemaNCuerpos::energia_cinetica() const {
    double ke = 0.0;
    // reduction(+:ke): cada hilo acumula en una copia privada y OpenMP las suma
    // al final. Sin esto, todos escribirian la misma variable a la vez.
    #pragma omp parallel for reduction(+:ke) schedule(static)
    for (int i = 0; i < n_; ++i) {
        ke += 0.5 * masa_[i] * (static_cast<double>(vx_[i]) * vx_[i] +
                                static_cast<double>(vy_[i]) * vy_[i]);
    }
    return ke;
}

double SistemaNCuerpos::energia_potencial() const {
    const double G = fisica_.gravedad;
    const double eps2 = fisica_.softening * fisica_.softening;
    double pe = 0.0;

    // Potencial de Plummer: U = -G*m_i*m_j / sqrt(r^2 + eps^2). Es exactamente
    // el potencial cuyo gradiente da la fuerza suavizada de arriba, por eso
    // KE + PE si tiene sentido como cantidad conservada.
    // Cada par se cuenta una sola vez (j > i).
    //
    // Aqui SI conviene schedule(dynamic): como el bucle interno arranca en i+1,
    // la iteracion i = 0 hace N trabajos y la i = N-1 no hace ninguno. Con static
    // el hilo que reciba el bloque final quedaria ocioso. Es el contraste exacto
    // con el bucle de fuerzas, donde la carga si es uniforme.
    #pragma omp parallel for reduction(+:pe) schedule(dynamic, 64)
    for (int i = 0; i < n_; ++i) {
        for (int j = i + 1; j < n_; ++j) {
            const double dx = static_cast<double>(px_[j]) - px_[i];
            const double dy = static_cast<double>(py_[j]) - py_[i];
            pe -= G * masa_[i] * masa_[j] / std::sqrt(dx * dx + dy * dy + eps2);
        }
    }
    return pe;
}
