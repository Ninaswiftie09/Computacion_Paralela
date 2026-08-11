#ifndef INTERNO_H
#define INTERNO_H

#include "ecosistema.h"

/* =========================================================
 * PARAMETROS BIOLOGICOS
 *
 * Son constantes de compilacion porque describen la especie,
 * no la corrida. Lo que cambia entre corridas (tamano,
 * poblaciones, ticks, probabilidades) vive en Configuracion.
 * ========================================================= */

#define EDAD_MAXIMA_ALGA        30
#define EDAD_MAXIMA_CARACOL     15
#define EDAD_MAXIMA_ANGUILA     35

#define TICKS_MAX_SIN_COMER_CARACOL   6
#define TICKS_MAX_SIN_COMER_ANGUILA   12

#define COSTO_ENERGIA_METABOLICA_CARACOL  1
#define COSTO_ENERGIA_METABOLICA_ANGUILA  1

#define ENERGIA_GANADA_CARACOL_POR_ALGA      3
#define ENERGIA_GANADA_ANGUILA_POR_CARACOL   7

#define UMBRAL_REPRODUCCION_MULT_CARACOL   3
#define UMBRAL_REPRODUCCION_MULT_ANGUILA   2

/*
 * Cantidad minima de presas consumidas para poder
 * reproducirse. El PDF de reglas lo pide de forma explicita:
 * un herbivoro se reproduce "si ha consumido al menos una
 * cierta cantidad de plantas".
 */
#define COMIDAS_MINIMAS_CARACOL   2
#define COMIDAS_MINIMAS_ANGUILA   1

/*
 * Muerte de plantas por encierro: un alga rodeada por
 * completo de otras algas no tiene espacio para crecer y
 * muere. Tambien lo pide el PDF de reglas.
 */
#define ALGA_MUERE_POR_ENCIERRO   1


/* =========================================================
 * UTILIDADES COMPARTIDAS ENTRE MODULOS
 * ========================================================= */

size_t eco_indice_celda(
    const Ecosistema *eco,
    int fila,
    int columna
);

int eco_posicion_valida(
    const Ecosistema *eco,
    int fila,
    int columna
);

Organismo eco_organismo_vacio(void);

Organismo eco_crear_organismo(
    Ecosistema *eco,
    TipoEspecie tipo
);

uint32_t eco_siguiente_aleatorio(
    Ecosistema *eco
);

char eco_simbolo_especie(
    TipoEspecie tipo,
    EstiloSimbolos estilo
);

size_t eco_capacidad(
    const Ecosistema *eco
);

/*
 * Cantidad de cerrojos reservados. Se usa striping: cuando la
 * cuadricula es enorme, varias celdas comparten cerrojo. Cada
 * cerrojo ocupa una linea de cache completa para que dos
 * hilos vecinos no se peleen por la misma.
 */
#define CERROJOS_MAXIMOS 65536

size_t eco_num_cerrojos(
    const Ecosistema *eco
);

#ifdef _OPENMP

#include <omp.h>

/*
 * Cerrojo propio de tipo test-and-set en vez de omp_lock_t.
 *
 * Medido sobre una cuadricula de 1024x1024, omp_lock_t se
 * llevaba el 43% del tiempo de ejecucion: cada operacion es
 * una llamada a la biblioteca de OpenMP. Aqui la toma del
 * cerrojo se reduce a un intercambio atomico, que en x86 es
 * una sola instruccion.
 *
 * El relleno lleva cada cerrojo a una linea de cache propia.
 * Sin el, los cerrojos de celdas contiguas comparten linea y
 * los hilos se invalidan la cache entre si aunque trabajen en
 * celdas distintas (false sharing).
 */
typedef union {

    volatile int tomado;

    char relleno[64];

} CerrojoAlineado;

#endif


/* =========================================================
 * CONTEXTO DE EJECUCION DE UN TICK
 *
 * Las reglas del ecosistema estan escritas una sola vez y se
 * ejecutan igual en secuencial y en paralelo. El contexto es
 * lo que abstrae las dos diferencias:
 *
 *   1. De donde sale la aleatoriedad. En secuencial se usa el
 *      generador global del ecosistema, para que el resultado
 *      sea identico al del motor original. En paralelo cada
 *      hilo tiene su propio stream y no toca rng_estado.
 *
 *   2. Como se protegen las escrituras sobre la cuadricula.
 *      En secuencial los cerrojos son operaciones vacias; en
 *      paralelo se toman de verdad, siempre en orden
 *      ascendente de indice para no poder generar deadlock.
 * ========================================================= */

typedef struct {

    Ecosistema *eco;

    /* Estado local del generador (solo en modo paralelo) */
    uint32_t rng;

    int paralelo;

    /*
     * Acciones que se perdieron porque otro organismo gano la
     * celda primero. No es un error: es la competencia por
     * recursos resuelta.
     */
    long long conflictos;

} ContextoTick;


void ctx_iniciar(
    ContextoTick *ctx,
    Ecosistema *eco,
    int paralelo,
    int hilo
);

uint32_t ctx_aleatorio(
    ContextoTick *ctx
);

int ctx_aleatorio_entero(
    ContextoTick *ctx,
    int minimo,
    int maximo
);

int ctx_ocurre(
    ContextoTick *ctx,
    double probabilidad
);

/*
 * Toma (o libera) los cerrojos de dos celdas. Acepta que
 * ambos indices sean iguales. En modo secuencial no hace
 * nada.
 */
void ctx_bloquear_par(
    ContextoTick *ctx,
    size_t indice_a,
    size_t indice_b
);

void ctx_desbloquear_par(
    ContextoTick *ctx,
    size_t indice_a,
    size_t indice_b
);


/* =========================================================
 * MEZCLA DE ORGANISMOS
 * ========================================================= */

/*
 * Mezcla el snapshot para que el orden de atencion no dependa
 * de la posicion en la cuadricula. En paralelo la mezcla se
 * hace por bloques, uno por hilo, porque Fisher-Yates global
 * es estrictamente secuencial y se convertiria en el cuello
 * de botella de Amdahl.
 */
void eco_mezclar_snapshot(
    Ecosistema *eco,
    EntradaSnapshot *lista,
    size_t cantidad,
    int paralelo
);

size_t eco_recolectar_snapshot(
    Ecosistema *eco,
    TipoEspecie tipo,
    EntradaSnapshot *lista,
    int paralelo
);


#endif
