/* =========================================================
 * ecosistema.c
 *
 * Motor base del ecosistema: estructuras, cuadricula,
 * inicializacion, acceso a celdas, vecindad de Moore,
 * generacion de numeros aleatorios, conteo de poblaciones y
 * validacion de invariantes.
 *
 * Las reglas de comportamiento estan en reglas.c y toda la
 * impresion en salida.c.
 * ========================================================= */

#include "interno.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif


/* =========================================================
 * UTILIDADES INTERNAS
 * ========================================================= */

size_t eco_indice_celda(
    const Ecosistema *eco,
    int fila,
    int columna
) {
    return
        (size_t)fila * (size_t)eco->config.columnas
        + (size_t)columna;
}


int eco_posicion_valida(
    const Ecosistema *eco,
    int fila,
    int columna
) {
    return
        fila >= 0 &&
        fila < eco->config.filas &&
        columna >= 0 &&
        columna < eco->config.columnas;
}


size_t eco_capacidad(
    const Ecosistema *eco
) {
    return
        (size_t)eco->config.filas
        *
        (size_t)eco->config.columnas;
}


size_t eco_num_cerrojos(
    const Ecosistema *eco
) {

    size_t capacidad;


    capacidad = eco_capacidad(eco);


    if (capacidad < (size_t)CERROJOS_MAXIMOS) {

        return capacidad;
    }


    return (size_t)CERROJOS_MAXIMOS;
}


Organismo eco_organismo_vacio(void) {

    Organismo organismo;

    organismo.id = 0;

    organismo.tipo = ESPECIE_VACIA;

    organismo.energia = 0;

    organismo.edad = 0;

    organismo.comidas = 0;

    organismo.ticks_sin_comer = 0;

    return organismo;
}


Organismo eco_crear_organismo(
    Ecosistema *eco,
    TipoEspecie tipo
) {

    Organismo organismo;

    uint64_t id_asignado;


    organismo = eco_organismo_vacio();


    /*
     * El contador de ids es el unico dato verdaderamente
     * global que tocan todos los hilos. Se incrementa de
     * forma atomica para que dos nacimientos simultaneos no
     * reciban el mismo id.
     */
#ifdef _OPENMP
#pragma omp atomic capture
#endif
    id_asignado = eco->siguiente_id++;


    organismo.id = id_asignado;

    organismo.tipo = tipo;


    if (tipo == ESPECIE_CARACOL) {

        organismo.energia =
            eco->config.energia_inicial_caracol;

    }
    else if (tipo == ESPECIE_ANGUILA) {

        organismo.energia =
            eco->config.energia_inicial_anguila;
    }

    return organismo;
}


/* =========================================================
 * GENERADOR PSEUDOALEATORIO
 *
 * xorshift32: barato, con periodo suficiente para la escala
 * de la simulacion y facil de replicar en el informe.
 * ========================================================= */

uint32_t eco_siguiente_aleatorio(
    Ecosistema *eco
) {

    uint32_t x;

    x = eco->rng_estado;

    x ^= x << 13;

    x ^= x >> 17;

    x ^= x << 5;

    eco->rng_estado = x;

    return x;
}


/*
 * Avanza un estado local, sin tocar el ecosistema. Es la
 * version que usa cada hilo sobre su propio stream.
 */
static uint32_t avanzar_estado(
    uint32_t *estado
) {

    uint32_t x;

    x = *estado;

    x ^= x << 13;

    x ^= x >> 17;

    x ^= x << 5;

    *estado = x;

    return x;
}


/*
 * Mezclador tipo splitmix para derivar semillas independientes
 * por hilo y por tick. Sin esto, dos hilos podrian arrancar
 * con estados parecidos y producir decisiones correlacionadas.
 */
static uint32_t derivar_semilla(
    uint32_t semilla,
    int tick,
    int hilo
) {

    uint64_t z;


    z =
        (uint64_t)semilla
        +
        0x9E3779B97F4A7C15ULL * (uint64_t)(tick + 1)
        +
        0xBF58476D1CE4E5B9ULL * (uint64_t)(hilo + 1);


    z ^= z >> 30;

    z *= 0xBF58476D1CE4E5B9ULL;

    z ^= z >> 27;

    z *= 0x94D049BB133111EBULL;

    z ^= z >> 31;


    /*
     * xorshift32 no admite el estado cero.
     */
    if ((uint32_t)z == 0u) {

        return 0xA341316Cu;
    }


    return (uint32_t)z;
}


/* =========================================================
 * MEZCLA DE INDICES (distribucion inicial)
 * ========================================================= */

static void mezclar_indices(
    Ecosistema *eco,
    size_t *indices,
    size_t cantidad
) {

    size_t i;

    if (cantidad < 2) {
        return;
    }


    for (i = cantidad - 1; i > 0; --i) {

        size_t j;

        size_t temporal;

        j =
            (size_t)eco_siguiente_aleatorio(eco)
            % (i + 1);

        temporal = indices[i];

        indices[i] = indices[j];

        indices[j] = temporal;
    }
}


/* =========================================================
 * VALIDACION DE CONFIGURACION
 * ========================================================= */

static int validar_configuracion(
    const Configuracion *config
) {

    long long capacidad;

    long long total_inicial;


    if (config == NULL) {

        fprintf(
            stderr,
            "Error: la configuracion es NULL.\n"
        );

        return 0;
    }


    if (
        config->filas <= 0 ||
        config->columnas <= 0
    ) {

        fprintf(
            stderr,
            "Error: filas y columnas deben ser mayores que cero.\n"
        );

        return 0;
    }


    if (
        config->algas_iniciales < 0 ||
        config->caracoles_iniciales < 0 ||
        config->anguilas_iniciales < 0 ||
        config->numero_ticks < 0
    ) {

        fprintf(
            stderr,
            "Error: las cantidades no pueden ser negativas.\n"
        );

        return 0;
    }


    capacidad =
        (long long)config->filas
        * (long long)config->columnas;


    total_inicial =
        (long long)config->algas_iniciales
        +
        (long long)config->caracoles_iniciales
        +
        (long long)config->anguilas_iniciales;


    if (total_inicial > capacidad) {

        fprintf(
            stderr,
            "Error: hay %lld organismos para solo %lld celdas.\n",
            total_inicial,
            capacidad
        );

        return 0;
    }


    /*
     * Las probabilidades deben estar
     * entre 0 y 1.
     */
    if (
        config->prob_reproduccion_alga < 0.0 ||
        config->prob_reproduccion_alga > 1.0 ||

        config->prob_reproduccion_caracol < 0.0 ||
        config->prob_reproduccion_caracol > 1.0 ||

        config->prob_reproduccion_anguila < 0.0 ||
        config->prob_reproduccion_anguila > 1.0
    ) {

        fprintf(
            stderr,
            "Error: las probabilidades deben estar entre 0 y 1.\n"
        );

        return 0;
    }


    return 1;
}


/* =========================================================
 * SIMBOLOS
 * ========================================================= */

char eco_simbolo_especie(
    TipoEspecie tipo,
    EstiloSimbolos estilo
) {

    if (estilo == SIMBOLOS_PDF) {

        switch (tipo) {

            case ESPECIE_ALGA:

                return 'P';


            case ESPECIE_CARACOL:

                return 'H';


            case ESPECIE_ANGUILA:

                return 'C';


            case ESPECIE_VACIA:

            default:

                return '.';
        }
    }


    switch (tipo) {

        case ESPECIE_ALGA:

            return 'A';


        case ESPECIE_CARACOL:

            /*
             * G de Gary.
             */
            return 'G';


        case ESPECIE_ANGUILA:

            return 'E';


        case ESPECIE_VACIA:

        default:

            return '.';
    }
}


/* =========================================================
 * DISTRIBUCION INICIAL
 * ========================================================= */

static int colocar_poblacion_inicial(
    Ecosistema *eco
) {

    size_t capacidad;

    size_t *indices;

    size_t cursor;

    size_t i;

    int n;


    capacidad = eco_capacidad(eco);


    indices =
        (size_t *)malloc(
            capacidad * sizeof(size_t)
        );


    if (indices == NULL) {

        fprintf(
            stderr,
            "Error: no se pudo reservar memoria "
            "para la distribucion inicial.\n"
        );

        return 0;
    }


    /*
     * Llenamos el arreglo con todos
     * los indices posibles.
     */
    for (i = 0; i < capacidad; ++i) {

        indices[i] = i;
    }


    /*
     * Revolvemos las posiciones.
     */
    mezclar_indices(
        eco,
        indices,
        capacidad
    );


    cursor = 0;


    /*
     * Primero colocamos las algas.
     */
    for (
        n = 0;
        n < eco->config.algas_iniciales;
        ++n
    ) {

        eco->celdas[
            indices[cursor++]
        ].organismo =
            eco_crear_organismo(
                eco,
                ESPECIE_ALGA
            );
    }


    /*
     * Luego los caracoles.
     */
    for (
        n = 0;
        n < eco->config.caracoles_iniciales;
        ++n
    ) {

        eco->celdas[
            indices[cursor++]
        ].organismo =
            eco_crear_organismo(
                eco,
                ESPECIE_CARACOL
            );
    }


    /*
     * Finalmente las anguilas.
     */
    for (
        n = 0;
        n < eco->config.anguilas_iniciales;
        ++n
    ) {

        eco->celdas[
            indices[cursor++]
        ].organismo =
            eco_crear_organismo(
                eco,
                ESPECIE_ANGUILA
            );
    }


    free(indices);

    return 1;
}


/* =========================================================
 * CONFIGURACION DE FONDO DE BIKINI
 * ========================================================= */

Configuracion configuracion_fondo_bikini(void) {

    Configuracion config;


    /*
     * Empezamos toda la estructura en cero.
     */
    memset(
        &config,
        0,
        sizeof(config)
    );


    strcpy(
        config.nombre,
        "Ecosistema"
    );


    /*
     * Dimensiones.
     */
    config.filas = 12;

    config.columnas = 24;


    /*
     * Poblaciones iniciales.
     */
    config.algas_iniciales = 78;

    config.caracoles_iniciales = 20;

    config.anguilas_iniciales = 7;


    /*
     * Las mismas poblaciones expresadas como fraccion de la
     * cuadricula, para poder crecer el problema sin cambiar
     * la ecologia.
     */
    config.densidad_algas = 78.0 / 288.0;

    config.densidad_caracoles = 20.0 / 288.0;

    config.densidad_anguilas = 7.0 / 288.0;


    /*
     * Cantidad de ticks.
     */
    config.numero_ticks = 10;


    /*
     * Probabilidades.
     */
    config.prob_reproduccion_alga = 0.30;

    config.prob_reproduccion_caracol = 0.20;

    config.prob_reproduccion_anguila = 0.12;


    /*
     * Energia inicial preparada para
     * herbivoros y carnivoros.
     */
    config.energia_inicial_caracol = 4;

    config.energia_inicial_anguila = 6;


    /*
     * Semilla fija.
     *
     * Si no cambiamos este numero,
     * obtendremos la misma cuadricula
     * inicial cada vez.
     */
    config.semilla = 20260809u;


    /*
     * Ejecucion.
     */
    config.modo = MODO_SECUENCIAL;

    config.num_hilos = 0;


    /*
     * Salida.
     */
    config.mostrar_presentacion = 1;

    config.mostrar_cuadricula_cada_tick = 1;

    config.silencioso = 0;

    config.ticks_por_reporte = 1;

    /*
     * Por defecto se usa la notacion del enunciado (P, H, C).
     * Los simbolos tematicos quedan disponibles con
     * --simbolos bikini.
     */
    config.simbolos = SIMBOLOS_PDF;

    config.ruta_resultados[0] = '\0';

    config.ruta_exportacion[0] = '\0';


    config.validar_cada_tick = 0;


    return config;
}


void configuracion_aplicar_densidades(
    Configuracion *config
) {

    double capacidad;

    long long total;


    if (config == NULL) {

        return;
    }


    if (
        config->filas <= 0 ||
        config->columnas <= 0
    ) {

        return;
    }


    capacidad =
        (double)config->filas
        *
        (double)config->columnas;


    /*
     * Convertir un double mayor que INT_MAX a int es
     * comportamiento indefinido, asi que se recorta antes.
     * A esta escala la reserva de memoria ya habria fallado,
     * pero conviene no depender de eso.
     */
    if (capacidad > 2.0e9) {

        capacidad = 2.0e9;
    }


    config->algas_iniciales =
        (int)(config->densidad_algas * capacidad);

    config->caracoles_iniciales =
        (int)(config->densidad_caracoles * capacidad);

    config->anguilas_iniciales =
        (int)(config->densidad_anguilas * capacidad);


    /*
     * El redondeo podria dejarnos con mas organismos que
     * celdas en cuadriculas muy pequenas.
     */
    total =
        (long long)config->algas_iniciales
        +
        (long long)config->caracoles_iniciales
        +
        (long long)config->anguilas_iniciales;


    while (
        total > (long long)capacidad &&
        config->algas_iniciales > 0
    ) {

        --config->algas_iniciales;

        --total;
    }
}


/* =========================================================
 * INICIALIZACION
 * ========================================================= */

static int reservar_cerrojos(
    Ecosistema *eco
) {

#ifdef _OPENMP

    size_t cantidad;

    size_t i;

    CerrojoAlineado *cerrojos;


    cantidad = eco_num_cerrojos(eco);


    cerrojos =
        (CerrojoAlineado *)malloc(
            cantidad * sizeof(CerrojoAlineado)
        );


    if (cerrojos == NULL) {

        fprintf(
            stderr,
            "Error: no se pudieron reservar los cerrojos.\n"
        );

        return 0;
    }


    for (i = 0; i < cantidad; ++i) {

        cerrojos[i].tomado = 0;
    }


    eco->cerrojos = (void *)cerrojos;

    return 1;

#else

    /*
     * Sin OpenMP no hay nada que proteger.
     */
    eco->cerrojos = NULL;

    return 1;

#endif
}


static void liberar_cerrojos(
    Ecosistema *eco
) {

#ifdef _OPENMP

    if (eco->cerrojos == NULL) {

        return;
    }


    /*
     * Son enteros simples: basta con devolver la memoria.
     */
    free(eco->cerrojos);

#endif

    eco->cerrojos = NULL;
}


int ecosistema_inicializar(
    Ecosistema *eco,
    const Configuracion *config
) {

    size_t capacidad;

    size_t i;


    if (
        eco == NULL ||
        !validar_configuracion(config)
    ) {

        return 0;
    }


    /*
     * Limpiamos toda la estructura.
     */
    memset(
        eco,
        0,
        sizeof(*eco)
    );


    /*
     * Copiamos la configuracion.
     */
    eco->config = *config;


    capacidad =
        (size_t)config->filas
        *
        (size_t)config->columnas;


    /*
     * Reservamos memoria dinamicamente
     * para la cuadricula.
     */
    eco->celdas =
        (Celda *)malloc(
            capacidad * sizeof(Celda)
        );


    if (eco->celdas == NULL) {

        fprintf(
            stderr,
            "Error: no se pudo crear "
            "la cuadricula del ecosistema.\n"
        );

        return 0;
    }


    /*
     * Buffer unico para los snapshots. Se reserva aqui y se
     * reutiliza en los tres barridos de cada tick, en vez de
     * pedir y devolver memoria nueve veces por segundo.
     */
    eco->snapshot =
        (EntradaSnapshot *)malloc(
            capacidad * sizeof(EntradaSnapshot)
        );


    if (eco->snapshot == NULL) {

        fprintf(
            stderr,
            "Error: no se pudo reservar el buffer "
            "de organismos por tick.\n"
        );

        ecosistema_liberar(eco);

        return 0;
    }


    if (eco->config.modo == MODO_PARALELO) {

        if (!reservar_cerrojos(eco)) {

            ecosistema_liberar(eco);

            return 0;
        }
    }


    eco->siguiente_id = 1;

    eco->tick_actual = 0;

    eco->tiempo_computo = 0.0;

    eco->hilos_utilizados = 1;


    /*
     * xorshift32 no puede iniciar con estado cero.
     *
     * Si alguien configura semilla = 0,
     * usamos automaticamente otro valor.
     */
    if (config->semilla == 0u) {

        eco->rng_estado =
            0xA341316Cu;

    }
    else {

        eco->rng_estado =
            config->semilla;
    }


    /*
     * Inicialmente todas las posiciones
     * contienen agua.
     */
    for (i = 0; i < capacidad; ++i) {

        eco->celdas[i].organismo =
            eco_organismo_vacio();
    }


    /*
     * Colocamos los organismos iniciales.
     *
     * Este paso se mantiene secuencial a proposito: consume
     * el generador global en un orden fijo, de modo que la
     * cuadricula inicial es identica en ambos modos y las dos
     * versiones arrancan desde el mismo punto.
     */
    if (!colocar_poblacion_inicial(eco)) {

        ecosistema_liberar(eco);

        return 0;
    }


    return 1;
}


/* =========================================================
 * LIBERAR MEMORIA
 * ========================================================= */

void ecosistema_liberar(
    Ecosistema *eco
) {

    if (eco == NULL) {
        return;
    }


    liberar_cerrojos(eco);


    free(
        eco->snapshot
    );

    eco->snapshot = NULL;


    free(
        eco->celdas
    );


    eco->celdas = NULL;
}


/* =========================================================
 * ACCESO A CELDAS
 * ========================================================= */

Celda *ecosistema_acceder_celda(
    Ecosistema *eco,
    int fila,
    int columna
) {

    if (
        eco == NULL ||
        eco->celdas == NULL ||
        !eco_posicion_valida(
            eco,
            fila,
            columna
        )
    ) {

        return NULL;
    }


    return &eco->celdas[
        eco_indice_celda(
            eco,
            fila,
            columna
        )
    ];
}


const Celda *ecosistema_consultar_celda(
    const Ecosistema *eco,
    int fila,
    int columna
) {

    if (
        eco == NULL ||
        eco->celdas == NULL ||
        !eco_posicion_valida(
            eco,
            fila,
            columna
        )
    ) {

        return NULL;
    }


    return &eco->celdas[
        eco_indice_celda(
            eco,
            fila,
            columna
        )
    ];
}


/* =========================================================
 * ALEATORIEDAD
 * ========================================================= */

int ecosistema_aleatorio_entero(
    Ecosistema *eco,
    int minimo,
    int maximo
) {

    uint32_t rango;


    if (maximo <= minimo) {

        return minimo;
    }


    rango =
        (uint32_t)(
            maximo - minimo + 1
        );


    return
        minimo
        +
        (int)(
            eco_siguiente_aleatorio(eco)
            %
            rango
        );
}


double ecosistema_aleatorio_01(
    Ecosistema *eco
) {

    return
        (double)eco_siguiente_aleatorio(eco)
        /
        (double)UINT32_MAX;
}


int ecosistema_ocurre(
    Ecosistema *eco,
    double probabilidad
) {

    if (probabilidad <= 0.0) {

        return 0;
    }


    if (probabilidad >= 1.0) {

        return 1;
    }


    return
        ecosistema_aleatorio_01(eco)
        <
        probabilidad;
}


/* =========================================================
 * CONTEXTO DE EJECUCION
 * ========================================================= */

void ctx_iniciar(
    ContextoTick *ctx,
    Ecosistema *eco,
    int paralelo,
    int hilo
) {

    ctx->eco = eco;

    ctx->paralelo = paralelo;

    ctx->conflictos = 0;


    ctx->rng =
        derivar_semilla(
            eco->config.semilla,
            eco->tick_actual,
            hilo
        );
}


uint32_t ctx_aleatorio(
    ContextoTick *ctx
) {

    /*
     * En secuencial se consume el generador global para que
     * la corrida sea reproducible con la misma semilla y para
     * no cambiar el comportamiento del motor original.
     */
    if (!ctx->paralelo) {

        return eco_siguiente_aleatorio(ctx->eco);
    }


    return avanzar_estado(&ctx->rng);
}


int ctx_aleatorio_entero(
    ContextoTick *ctx,
    int minimo,
    int maximo
) {

    uint32_t rango;


    if (maximo <= minimo) {

        return minimo;
    }


    rango =
        (uint32_t)(
            maximo - minimo + 1
        );


    return
        minimo
        +
        (int)(
            ctx_aleatorio(ctx)
            %
            rango
        );
}


int ctx_ocurre(
    ContextoTick *ctx,
    double probabilidad
) {

    double sorteo;


    if (probabilidad <= 0.0) {

        return 0;
    }


    if (probabilidad >= 1.0) {

        return 1;
    }


    sorteo =
        (double)ctx_aleatorio(ctx)
        /
        (double)UINT32_MAX;


    return sorteo < probabilidad;
}


/* =========================================================
 * CERROJOS
 *
 * Siempre se toman en orden ascendente de indice. Con un
 * orden total sobre los cerrojos es imposible construir un
 * ciclo de espera, asi que no puede haber deadlock.
 * ========================================================= */

#ifdef _OPENMP

/*
 * Toma del cerrojo por test-and-set.
 *
 * "omp atomic capture" sobre la pareja (leer, escribir 1) se
 * compila a un intercambio atomico: si el valor anterior era
 * 0, el cerrojo era nuestro. El flush posterior garantiza que
 * las escrituras sobre la celda no se adelanten a la toma.
 *
 * Con striping de 65536 cerrojos la espera activa es rarisima:
 * dos hilos tienen que coincidir en la misma franja al mismo
 * tiempo.
 */
static void tomar_cerrojo(
    CerrojoAlineado *cerrojo
) {

    int anterior;


    do {

#pragma omp atomic capture
        {
            anterior = cerrojo->tomado;

            cerrojo->tomado = 1;
        }

    } while (anterior != 0);


#pragma omp flush
}


static void soltar_cerrojo(
    CerrojoAlineado *cerrojo
) {

#pragma omp flush

#pragma omp atomic write
    cerrojo->tomado = 0;
}

#endif


void ctx_bloquear_par(
    ContextoTick *ctx,
    size_t indice_a,
    size_t indice_b
) {

#ifdef _OPENMP

    CerrojoAlineado *cerrojos;

    size_t cantidad;

    size_t primero;

    size_t segundo;


    if (
        !ctx->paralelo ||
        ctx->eco->cerrojos == NULL
    ) {

        return;
    }


    cerrojos = (CerrojoAlineado *)ctx->eco->cerrojos;

    cantidad = eco_num_cerrojos(ctx->eco);


    primero = indice_a % cantidad;

    segundo = indice_b % cantidad;


    if (primero > segundo) {

        size_t temporal;

        temporal = primero;

        primero = segundo;

        segundo = temporal;
    }


    tomar_cerrojo(&cerrojos[primero]);


    /*
     * Con striping dos celdas distintas pueden caer en el
     * mismo cerrojo: en ese caso ya esta tomado.
     */
    if (segundo != primero) {

        tomar_cerrojo(&cerrojos[segundo]);
    }

#else

    (void)ctx;
    (void)indice_a;
    (void)indice_b;

#endif
}


void ctx_desbloquear_par(
    ContextoTick *ctx,
    size_t indice_a,
    size_t indice_b
) {

#ifdef _OPENMP

    CerrojoAlineado *cerrojos;

    size_t cantidad;

    size_t primero;

    size_t segundo;


    if (
        !ctx->paralelo ||
        ctx->eco->cerrojos == NULL
    ) {

        return;
    }


    cerrojos = (CerrojoAlineado *)ctx->eco->cerrojos;

    cantidad = eco_num_cerrojos(ctx->eco);


    primero = indice_a % cantidad;

    segundo = indice_b % cantidad;


    if (primero > segundo) {

        size_t temporal;

        temporal = primero;

        primero = segundo;

        segundo = temporal;
    }


    if (segundo != primero) {

        soltar_cerrojo(&cerrojos[segundo]);
    }


    soltar_cerrojo(&cerrojos[primero]);

#else

    (void)ctx;
    (void)indice_a;
    (void)indice_b;

#endif
}


/* =========================================================
 * VECINOS
 * ========================================================= */

int ecosistema_obtener_vecinos(
    const Ecosistema *eco,
    int fila,
    int columna,
    Posicion salida[MAX_VECINOS]
) {

    int df;

    int dc;

    int cantidad;


    cantidad = 0;


    if (
        eco == NULL ||
        salida == NULL ||
        !eco_posicion_valida(
            eco,
            fila,
            columna
        )
    ) {

        return 0;
    }


    /*
     * Utilizamos vecindad de Moore:
     *
     *   X X X
     *   X O X
     *   X X X
     *
     * O representa la celda actual.
     *
     * En bordes y esquinas simplemente
     * existen menos vecinos.
     */

    for (
        df = -1;
        df <= 1;
        ++df
    ) {

        for (
            dc = -1;
            dc <= 1;
            ++dc
        ) {

            int nueva_fila;

            int nueva_columna;


            /*
             * No contamos la misma celda.
             */
            if (
                df == 0 &&
                dc == 0
            ) {

                continue;
            }


            nueva_fila =
                fila + df;

            nueva_columna =
                columna + dc;


            if (
                eco_posicion_valida(
                    eco,
                    nueva_fila,
                    nueva_columna
                )
            ) {

                salida[cantidad].fila =
                    nueva_fila;

                salida[cantidad].columna =
                    nueva_columna;

                ++cantidad;
            }
        }
    }


    return cantidad;
}


/* =========================================================
 * VECINOS VACIOS
 * ========================================================= */

int ecosistema_obtener_vecinos_vacios(
    const Ecosistema *eco,
    int fila,
    int columna,
    Posicion salida[MAX_VECINOS]
) {

    Posicion vecinos[MAX_VECINOS];

    int cantidad_vecinos;

    int cantidad_salida;

    int i;


    cantidad_salida = 0;


    if (salida == NULL) {

        return 0;
    }


    cantidad_vecinos =
        ecosistema_obtener_vecinos(
            eco,
            fila,
            columna,
            vecinos
        );


    for (
        i = 0;
        i < cantidad_vecinos;
        ++i
    ) {

        const Celda *celda;


        celda =
            ecosistema_consultar_celda(
                eco,
                vecinos[i].fila,
                vecinos[i].columna
            );


        if (
            celda->organismo.tipo
            ==
            ESPECIE_VACIA
        ) {

            salida[cantidad_salida] =
                vecinos[i];

            ++cantidad_salida;
        }
    }


    return cantidad_salida;
}


/* =========================================================
 * VECINOS DE UNA ESPECIE
 * ========================================================= */

int ecosistema_obtener_vecinos_tipo(
    const Ecosistema *eco,
    int fila,
    int columna,
    TipoEspecie tipo,
    Posicion salida[MAX_VECINOS]
) {

    Posicion vecinos[MAX_VECINOS];

    int cantidad_vecinos;

    int cantidad_salida;

    int i;


    cantidad_salida = 0;


    if (salida == NULL) {

        return 0;
    }


    cantidad_vecinos =
        ecosistema_obtener_vecinos(
            eco,
            fila,
            columna,
            vecinos
        );


    for (
        i = 0;
        i < cantidad_vecinos;
        ++i
    ) {

        const Celda *celda;


        celda =
            ecosistema_consultar_celda(
                eco,
                vecinos[i].fila,
                vecinos[i].columna
            );


        if (
            celda->organismo.tipo
            ==
            tipo
        ) {

            salida[cantidad_salida] =
                vecinos[i];

            ++cantidad_salida;
        }
    }


    return cantidad_salida;
}


/* =========================================================
 * SNAPSHOT DE ORGANISMOS POR ESPECIE
 * ========================================================= */

size_t eco_recolectar_snapshot(
    Ecosistema *eco,
    TipoEspecie tipo,
    EntradaSnapshot *lista,
    int paralelo
) {

    size_t capacidad;

    size_t total;

    size_t i;


    capacidad = eco_capacidad(eco);

    total = 0;


    if (!paralelo) {

        for (i = 0; i < capacidad; ++i) {

            const Organismo *org;


            org = &eco->celdas[i].organismo;


            if (org->tipo == tipo) {

                lista[total].pos.fila =
                    (int)(i / (size_t)eco->config.columnas);

                lista[total].pos.columna =
                    (int)(i % (size_t)eco->config.columnas);

                lista[total].id = org->id;

                ++total;
            }
        }

        return total;
    }


#ifdef _OPENMP

    /*
     * Version paralela: cada hilo cuenta primero cuantos
     * organismos le tocan, se calcula el desplazamiento con
     * una suma de prefijos y despues todos escriben a la vez
     * en zonas disjuntas del arreglo. Sin cerrojos y sin
     * escrituras solapadas.
     */
    {
        int hilos;

        size_t *conteos;

        size_t acumulado;


        hilos = omp_get_max_threads();


        conteos =
            (size_t *)calloc(
                (size_t)hilos + 1,
                sizeof(size_t)
            );


        if (conteos == NULL) {

            /*
             * Sin memoria auxiliar no vale la pena fallar:
             * se recolecta de forma secuencial.
             */
            return
                eco_recolectar_snapshot(
                    eco,
                    tipo,
                    lista,
                    0
                );
        }


#pragma omp parallel num_threads(hilos)
        {
            int mi_hilo;

            size_t inicio;

            size_t fin;

            size_t j;

            size_t propio;


            mi_hilo = omp_get_thread_num();

            inicio =
                (capacidad * (size_t)mi_hilo)
                / (size_t)hilos;

            fin =
                (capacidad * (size_t)(mi_hilo + 1))
                / (size_t)hilos;


            propio = 0;

            for (j = inicio; j < fin; ++j) {

                if (eco->celdas[j].organismo.tipo == tipo) {

                    ++propio;
                }
            }

            conteos[mi_hilo + 1] = propio;


#pragma omp barrier
#pragma omp single
            {
                int k;

                for (k = 1; k <= hilos; ++k) {

                    conteos[k] += conteos[k - 1];
                }
            }


            {
                size_t cursor;

                cursor = conteos[mi_hilo];

                for (j = inicio; j < fin; ++j) {

                    const Organismo *org;


                    org = &eco->celdas[j].organismo;


                    if (org->tipo == tipo) {

                        lista[cursor].pos.fila =
                            (int)(j / (size_t)eco->config.columnas);

                        lista[cursor].pos.columna =
                            (int)(j % (size_t)eco->config.columnas);

                        lista[cursor].id = org->id;

                        ++cursor;
                    }
                }
            }
        }


        acumulado = conteos[hilos];

        free(conteos);

        return acumulado;
    }

#else

    return
        eco_recolectar_snapshot(
            eco,
            tipo,
            lista,
            0
        );

#endif
}


/* =========================================================
 * MEZCLA DEL SNAPSHOT
 * ========================================================= */

#ifdef _OPENMP

/*
 * Mezcla un tramo del arreglo con un estado local. Solo la
 * necesita la version paralela.
 */
static void mezclar_rango(
    uint32_t *estado,
    EntradaSnapshot *lista,
    size_t inicio,
    size_t fin
) {

    size_t i;


    if (fin - inicio < 2) {

        return;
    }


    for (i = fin - 1; i > inicio; --i) {

        size_t j;

        EntradaSnapshot temporal;


        j =
            inicio
            +
            (size_t)(
                avanzar_estado(estado)
                %
                (uint32_t)(i - inicio + 1)
            );

        temporal = lista[i];

        lista[i] = lista[j];

        lista[j] = temporal;
    }
}

#endif


void eco_mezclar_snapshot(
    Ecosistema *eco,
    EntradaSnapshot *lista,
    size_t cantidad,
    int paralelo
) {

    if (cantidad < 2) {

        return;
    }


    if (!paralelo) {

        size_t i;

        /*
         * Fisher-Yates clasico consumiendo el generador
         * global, igual que el motor original.
         */
        for (i = cantidad - 1; i > 0; --i) {

            size_t j;

            EntradaSnapshot temporal;


            j =
                (size_t)ecosistema_aleatorio_entero(
                    eco,
                    0,
                    (int)i
                );

            temporal = lista[i];

            lista[i] = lista[j];

            lista[j] = temporal;
        }

        return;
    }


#ifdef _OPENMP

    /*
     * Mezcla por bloques: cada hilo revuelve su propio tramo.
     * No produce una permutacion uniforme global, pero cumple
     * el mismo objetivo (que la prioridad no dependa de la
     * posicion en la cuadricula) sin serializar el tick.
     */
#pragma omp parallel
    {
        int hilos;

        int mi_hilo;

        size_t inicio;

        size_t fin;

        uint32_t estado;


        hilos = omp_get_num_threads();

        mi_hilo = omp_get_thread_num();


        inicio =
            (cantidad * (size_t)mi_hilo)
            / (size_t)hilos;

        fin =
            (cantidad * (size_t)(mi_hilo + 1))
            / (size_t)hilos;


        estado =
            derivar_semilla(
                eco->config.semilla ^ 0x5BF03635u,
                eco->tick_actual,
                mi_hilo
            );


        mezclar_rango(
            &estado,
            lista,
            inicio,
            fin
        );
    }

#endif
}


/* =========================================================
 * CONTEO DE POBLACION
 * ========================================================= */

Poblacion ecosistema_contar_poblacion(
    const Ecosistema *eco
) {

    Poblacion poblacion;

    size_t capacidad;

    size_t i;

    long long algas;

    long long caracoles;

    long long anguilas;

    long long vacias;


    memset(
        &poblacion,
        0,
        sizeof(poblacion)
    );


    if (
        eco == NULL ||
        eco->celdas == NULL
    ) {

        return poblacion;
    }


    capacidad = eco_capacidad(eco);


    algas = 0;

    caracoles = 0;

    anguilas = 0;

    vacias = 0;


    /*
     * Un conteo es una reduccion pura: cada hilo acumula sus
     * parciales y OpenMP los suma al final. No hay escrituras
     * compartidas ni necesidad de cerrojos.
     */
#ifdef _OPENMP
#pragma omp parallel for schedule(static) \
    reduction(+:algas, caracoles, anguilas, vacias)
#endif
    for (i = 0; i < capacidad; ++i) {

        switch (
            eco->celdas[i].organismo.tipo
        ) {

            case ESPECIE_ALGA:

                ++algas;

                break;


            case ESPECIE_CARACOL:

                ++caracoles;

                break;


            case ESPECIE_ANGUILA:

                ++anguilas;

                break;


            case ESPECIE_VACIA:

            default:

                ++vacias;

                break;
        }
    }


    poblacion.algas = (int)algas;

    poblacion.caracoles = (int)caracoles;

    poblacion.anguilas = (int)anguilas;

    poblacion.vacias = (int)vacias;


    return poblacion;
}


/* =========================================================
 * VALIDACION DE COHERENCIA
 *
 * Estos contadores son el detector de race conditions del
 * proyecto. Si la version paralela pierde una actualizacion o
 * duplica un organismo, aparece aqui.
 * ========================================================= */

static int comparar_ids(
    const void *a,
    const void *b
) {

    uint64_t ia;

    uint64_t ib;


    ia = *(const uint64_t *)a;

    ib = *(const uint64_t *)b;


    if (ia < ib) {

        return -1;
    }


    if (ia > ib) {

        return 1;
    }


    return 0;
}


long long ecosistema_validar_coherencia(
    Ecosistema *eco
) {

    size_t capacidad;

    size_t i;

    long long residuales;

    long long negativas;

    long long duplicados;

    uint64_t *ids;

    size_t total_ids;


    if (
        eco == NULL ||
        eco->celdas == NULL
    ) {

        return 0;
    }


    capacidad = eco_capacidad(eco);

    residuales = 0;

    negativas = 0;

    duplicados = 0;


    ids =
        (uint64_t *)malloc(
            capacidad * sizeof(uint64_t)
        );

    total_ids = 0;


    for (i = 0; i < capacidad; ++i) {

        const Organismo *org;


        org = &eco->celdas[i].organismo;


        if (org->tipo == ESPECIE_VACIA) {

            if (
                org->energia != 0 ||
                org->edad != 0 ||
                org->comidas != 0 ||
                org->ticks_sin_comer != 0 ||
                org->id != 0
            ) {

                ++residuales;
            }

            continue;
        }


        if (
            org->tipo != ESPECIE_ALGA &&
            org->energia < 0
        ) {

            ++negativas;
        }


        if (ids != NULL) {

            ids[total_ids++] = org->id;
        }
    }


    /*
     * Dos celdas con el mismo id significan que un organismo
     * fue copiado en lugar de movido: exactamente el sintoma
     * de una escritura perdida.
     */
    if (
        ids != NULL &&
        total_ids > 1
    ) {

        qsort(
            ids,
            total_ids,
            sizeof(uint64_t),
            comparar_ids
        );


        for (i = 1; i < total_ids; ++i) {

            if (ids[i] == ids[i - 1]) {

                ++duplicados;
            }
        }
    }


    free(ids);


    eco->diag.celdas_residuales += residuales;

    eco->diag.energias_negativas += negativas;

    eco->diag.ids_duplicados += duplicados;


    if (residuales > 0) {

        fprintf(
            stderr,
            "Advertencia [tick %d]: %lld celdas vacias con "
            "datos residuales.\n",
            eco->tick_actual,
            residuales
        );
    }


    if (negativas > 0) {

        fprintf(
            stderr,
            "Advertencia [tick %d]: %lld organismos con "
            "energia negativa.\n",
            eco->tick_actual,
            negativas
        );
    }


    if (duplicados > 0) {

        fprintf(
            stderr,
            "Advertencia [tick %d]: %lld ids duplicados "
            "(escritura perdida).\n",
            eco->tick_actual,
            duplicados
        );
    }


    return residuales + negativas + duplicados;
}
