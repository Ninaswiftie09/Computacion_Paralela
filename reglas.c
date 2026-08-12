/* =========================================================
 * reglas.c
 *
 * Reglas del ecosistema y ciclo de simulacion.
 *
 * Las reglas estan escritas UNA sola vez y se ejecutan igual
 * en modo secuencial y en modo paralelo. El ContextoTick es
 * el que decide de donde sale la aleatoriedad y si los
 * cerrojos son reales o no hacen nada.
 *
 * Protocolo de escritura sobre la cuadricula (modo paralelo):
 *
 *   1. Se lee la vecindad SIN cerrojos y se elige un destino.
 *      Es una lectura optimista: puede quedar desactualizada.
 *   2. Se toman los cerrojos de origen y destino, siempre en
 *      orden ascendente de indice (imposible el deadlock).
 *   3. Se REVALIDA: el origen debe seguir teniendo al mismo
 *      organismo (mismo id) y el destino debe seguir estando
 *      como se esperaba (vacio, o con la presa correcta).
 *   4. Si la revalidacion falla, la accion se descarta y se
 *      cuenta como conflicto. Eso es exactamente la
 *      "competencia por recursos" del enunciado: cuando dos
 *      organismos quieren la misma celda, solo uno la obtiene.
 *
 * Gracias al paso 3, una lectura desactualizada del paso 1
 * nunca puede corromper la cuadricula: a lo sumo hace que un
 * organismo pierda su turno.
 * ========================================================= */

#include "interno.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _OPENMP
#include <omp.h>
#else
#include <time.h>
#endif


/* =========================================================
 * RELOJ
 * ========================================================= */

static double ahora_segundos(void) {

#ifdef _OPENMP

    return omp_get_wtime();

#else

    return
        (double)clock()
        /
        (double)CLOCKS_PER_SEC;

#endif
}


/* =========================================================
 * DISTANCIA DE CHEBYSHEV
 *
 * En una vecindad de Moore un paso en diagonal cuesta lo
 * mismo que uno recto, asi que la distancia natural es el
 * maximo de las diferencias.
 * ========================================================= */

static int distancia_chebyshev(
    Posicion a,
    Posicion b
) {

    int df;

    int dc;


    df = a.fila - b.fila;

    dc = a.columna - b.columna;


    if (df < 0) {

        df = -df;
    }


    if (dc < 0) {

        dc = -dc;
    }


    return df > dc ? df : dc;
}


/* =========================================================
 * ELECCION DE DESTINO PARA HUIR
 *
 * El PDF de reglas pide que un herbivoro que detecta un
 * carnivoro adyacente pueda escapar a una celda vacia. Entre
 * las celdas libres se elige la que quede mas lejos del
 * depredador mas cercano; si varias empatan, se sortea entre
 * ellas para no introducir un sesgo direccional.
 * ========================================================= */

static int elegir_destino_huida(
    ContextoTick *ctx,
    const Posicion *vacios,
    int cantidad_vacios,
    const Posicion *depredadores,
    int cantidad_depredadores
) {

    int i;

    int j;

    int mejor_puntaje;

    int candidatos[MAX_VECINOS];

    int cantidad_candidatos;


    mejor_puntaje = -1;

    cantidad_candidatos = 0;


    for (i = 0; i < cantidad_vacios; ++i) {

        int puntaje;


        /*
         * Puntaje = distancia al depredador mas cercano.
         */
        puntaje = MAX_VECINOS + 1;


        for (j = 0; j < cantidad_depredadores; ++j) {

            int distancia;


            distancia =
                distancia_chebyshev(
                    vacios[i],
                    depredadores[j]
                );


            if (distancia < puntaje) {

                puntaje = distancia;
            }
        }


        if (puntaje > mejor_puntaje) {

            mejor_puntaje = puntaje;

            cantidad_candidatos = 0;

            candidatos[cantidad_candidatos++] = i;

        }
        else if (puntaje == mejor_puntaje) {

            candidatos[cantidad_candidatos++] = i;
        }
    }


    if (cantidad_candidatos == 0) {

        return 0;
    }


    return
        candidatos[
            ctx_aleatorio_entero(
                ctx,
                0,
                cantidad_candidatos - 1
            )
        ];
}


/* =========================================================
 * mover()
 *
 * Traslada al organismo a una celda vecina vacia. Si es un
 * herbivoro amenazado, prioriza alejarse del depredador.
 *
 * Devuelve 1 si se movio; en ese caso *nueva_posicion queda
 * con la celda nueva.
 * ========================================================= */

static int mover(
    ContextoTick *ctx,
    int fila,
    int columna,
    uint64_t id,
    Posicion *nueva_posicion
) {

    Ecosistema *eco;

    Posicion vecinos_vacios[MAX_VECINOS];

    Posicion depredadores[MAX_VECINOS];

    int cantidad;

    int cantidad_depredadores;

    int elegido;

    Posicion destino;

    Celda *origen;

    Celda *celda_destino;

    size_t indice_origen;

    size_t indice_destino;

    int movido;


    eco = ctx->eco;


    if (nueva_posicion != NULL) {

        nueva_posicion->fila = fila;

        nueva_posicion->columna = columna;
    }


    cantidad =
        ecosistema_obtener_vecinos_vacios(
            eco,
            fila,
            columna,
            vecinos_vacios
        );


    if (cantidad == 0) {

        return 0;
    }


    /*
     * Huida: solo aplica a los herbivoros.
     */
    cantidad_depredadores = 0;

    origen =
        ecosistema_acceder_celda(
            eco,
            fila,
            columna
        );


    if (
        origen != NULL &&
        origen->organismo.tipo == ESPECIE_CARACOL
    ) {

        cantidad_depredadores =
            ecosistema_obtener_vecinos_tipo(
                eco,
                fila,
                columna,
                ESPECIE_ANGUILA,
                depredadores
            );
    }


    if (cantidad_depredadores > 0) {

        elegido =
            elegir_destino_huida(
                ctx,
                vecinos_vacios,
                cantidad,
                depredadores,
                cantidad_depredadores
            );

    }
    else {

        elegido =
            ctx_aleatorio_entero(
                ctx,
                0,
                cantidad - 1
            );
    }


    destino = vecinos_vacios[elegido];


    indice_origen =
        eco_indice_celda(
            eco,
            fila,
            columna
        );

    indice_destino =
        eco_indice_celda(
            eco,
            destino.fila,
            destino.columna
        );


    movido = 0;


    ctx_bloquear_par(
        ctx,
        indice_origen,
        indice_destino
    );


    origen = &eco->celdas[indice_origen];

    celda_destino = &eco->celdas[indice_destino];


    /*
     * Revalidacion bajo cerrojo.
     */
    if (
        origen->organismo.id == id &&
        celda_destino->organismo.tipo == ESPECIE_VACIA
    ) {

        celda_destino->organismo =
            origen->organismo;

        origen->organismo =
            eco_organismo_vacio();

        movido = 1;
    }


    ctx_desbloquear_par(
        ctx,
        indice_origen,
        indice_destino
    );


    if (!movido) {

        ++ctx->conflictos;

        return 0;
    }


    if (nueva_posicion != NULL) {

        *nueva_posicion = destino;
    }


    return 1;
}


/* =========================================================
 * alimentar()
 *
 * Busca una presa vecina, la consume y ocupa su celda.
 * ========================================================= */

static int alimentar(
    ContextoTick *ctx,
    int fila,
    int columna,
    uint64_t id,
    TipoEspecie tipo_presa,
    int energia_ganada,
    Posicion *nueva_posicion
) {

    Ecosistema *eco;

    Posicion presas[MAX_VECINOS];

    int cantidad;

    Posicion destino;

    Celda *origen;

    Celda *celda_destino;

    size_t indice_origen;

    size_t indice_destino;

    int comio;


    eco = ctx->eco;


    if (nueva_posicion != NULL) {

        nueva_posicion->fila = fila;

        nueva_posicion->columna = columna;
    }


    cantidad =
        ecosistema_obtener_vecinos_tipo(
            eco,
            fila,
            columna,
            tipo_presa,
            presas
        );


    if (cantidad == 0) {

        return 0;
    }


    destino =
        presas[
            ctx_aleatorio_entero(
                ctx,
                0,
                cantidad - 1
            )
        ];


    indice_origen =
        eco_indice_celda(
            eco,
            fila,
            columna
        );

    indice_destino =
        eco_indice_celda(
            eco,
            destino.fila,
            destino.columna
        );


    comio = 0;


    ctx_bloquear_par(
        ctx,
        indice_origen,
        indice_destino
    );


    origen = &eco->celdas[indice_origen];

    celda_destino = &eco->celdas[indice_destino];


    /*
     * La presa concreta puede haber cambiado de individuo,
     * pero mientras siga habiendo una presa de la especie
     * correcta la accion es ecologicamente la misma.
     */
    if (
        origen->organismo.id == id &&
        celda_destino->organismo.tipo == tipo_presa
    ) {

        Organismo depredador;


        depredador = origen->organismo;

        depredador.energia += energia_ganada;

        depredador.comidas += 1;

        depredador.ticks_sin_comer = 0;


        celda_destino->organismo = depredador;

        origen->organismo = eco_organismo_vacio();

        comio = 1;
    }


    ctx_desbloquear_par(
        ctx,
        indice_origen,
        indice_destino
    );


    if (!comio) {

        ++ctx->conflictos;

        return 0;
    }


    if (nueva_posicion != NULL) {

        *nueva_posicion = destino;
    }


    return 1;
}


/* =========================================================
 * reproducir()
 *
 * Crea un hijo en una celda vecina vacia si se cumplen las
 * condiciones de la especie:
 *
 *   - Las algas solo necesitan espacio y suerte.
 *   - Herbivoros y carnivoros necesitan ademas energia
 *     suficiente y haber comido al menos cierta cantidad de
 *     presas, tal como pide el enunciado.
 *
 * El progenitor cede "energia_heredada" al hijo.
 * ========================================================= */

static int reproducir(
    ContextoTick *ctx,
    int fila,
    int columna,
    uint64_t id,
    double probabilidad,
    int energia_minima,
    int comidas_minimas,
    int energia_heredada
) {

    Ecosistema *eco;

    Celda *origen;

    Posicion vecinos_vacios[MAX_VECINOS];

    int cantidad;

    Posicion destino;

    Celda *celda_destino;

    TipoEspecie tipo_padre;

    size_t indice_origen;

    size_t indice_destino;

    int nacio;


    eco = ctx->eco;


    indice_origen =
        eco_indice_celda(
            eco,
            fila,
            columna
        );


    origen = &eco->celdas[indice_origen];


    if (
        origen->organismo.id != id ||
        origen->organismo.tipo == ESPECIE_VACIA
    ) {

        return 0;
    }


    tipo_padre = origen->organismo.tipo;


    if (tipo_padre != ESPECIE_ALGA) {

        if (origen->organismo.energia < energia_minima) {

            return 0;
        }


        if (origen->organismo.comidas < comidas_minimas) {

            return 0;
        }
    }


    if (!ctx_ocurre(ctx, probabilidad)) {

        return 0;
    }


    cantidad =
        ecosistema_obtener_vecinos_vacios(
            eco,
            fila,
            columna,
            vecinos_vacios
        );


    if (cantidad == 0) {

        return 0;
    }


    destino =
        vecinos_vacios[
            ctx_aleatorio_entero(
                ctx,
                0,
                cantidad - 1
            )
        ];


    indice_destino =
        eco_indice_celda(
            eco,
            destino.fila,
            destino.columna
        );


    nacio = 0;


    ctx_bloquear_par(
        ctx,
        indice_origen,
        indice_destino
    );


    origen = &eco->celdas[indice_origen];

    celda_destino = &eco->celdas[indice_destino];


    if (
        origen->organismo.id == id &&
        celda_destino->organismo.tipo == ESPECIE_VACIA
    ) {

        celda_destino->organismo =
            eco_crear_organismo(
                eco,
                tipo_padre
            );


        if (tipo_padre != ESPECIE_ALGA) {

            origen->organismo.energia -=
                energia_heredada;
        }


        nacio = 1;
    }


    ctx_desbloquear_par(
        ctx,
        indice_origen,
        indice_destino
    );


    if (!nacio) {

        ++ctx->conflictos;
    }


    return nacio;
}


/* =========================================================
 * alga_encerrada()
 *
 * Una planta muere si no le queda espacio para crecer, es
 * decir si todos sus vecinos existentes son plantas.
 * ========================================================= */

static int alga_encerrada(
    Ecosistema *eco,
    int fila,
    int columna
) {

    Posicion vecinos[MAX_VECINOS];

    int cantidad;

    int i;


    cantidad =
        ecosistema_obtener_vecinos(
            eco,
            fila,
            columna,
            vecinos
        );


    if (cantidad == 0) {

        return 0;
    }


    for (i = 0; i < cantidad; ++i) {

        const Celda *celda;


        celda =
            ecosistema_consultar_celda(
                eco,
                vecinos[i].fila,
                vecinos[i].columna
            );


        if (celda->organismo.tipo != ESPECIE_ALGA) {

            return 0;
        }
    }


    return 1;
}


/* =========================================================
 * morir()
 *
 * Retira al organismo de la cuadricula si se cumple alguna
 * condicion de muerte: vejez, inanicion, energia agotada o
 * (para las plantas) falta de espacio.
 * ========================================================= */

static int morir(
    ContextoTick *ctx,
    int fila,
    int columna,
    uint64_t id,
    int edad_maxima,
    int ticks_max_sin_comer
) {

    Ecosistema *eco;

    Celda *celda;

    const Organismo *org;

    size_t indice;

    int debe_morir;

    int murio;


    eco = ctx->eco;


    indice =
        eco_indice_celda(
            eco,
            fila,
            columna
        );


    celda = &eco->celdas[indice];


    if (
        celda->organismo.id != id ||
        celda->organismo.tipo == ESPECIE_VACIA
    ) {

        return 0;
    }


    /*
     * Las condiciones se evaluan sin cerrojo: edad, energia y
     * hambre son campos que solo escribe este hilo (ver
     * envejecer / pagar_metabolismo).
     */
    org = &celda->organismo;

    debe_morir = 0;


    if (
        edad_maxima > 0 &&
        org->edad >= edad_maxima
    ) {

        debe_morir = 1;
    }


    if (org->tipo != ESPECIE_ALGA) {

        if (org->energia <= 0) {

            debe_morir = 1;
        }


        if (
            ticks_max_sin_comer > 0 &&
            org->ticks_sin_comer >= ticks_max_sin_comer
        ) {

            debe_morir = 1;
        }

    }
    else if (
        !debe_morir &&
        ALGA_MUERE_POR_ENCIERRO
    ) {

        /*
         * El encierro exige mirar la vecindad completa, asi
         * que solo se consulta si la planta sobrevivio a las
         * demas causas. Es una lectura optimista: en el peor
         * caso una planta muere un tick antes o despues de lo
         * ideal, pero la cuadricula nunca queda inconsistente.
         */
        debe_morir =
            alga_encerrada(
                eco,
                fila,
                columna
            );
    }


    if (!debe_morir) {

        return 0;
    }


    /*
     * Aplicar la muerte SI necesita cerrojo: al vaciar la
     * celda queda disponible para los demas, y esa transicion
     * debe ser visible de forma atomica.
     */
    murio = 0;


    ctx_bloquear_par(
        ctx,
        indice,
        indice
    );


    celda = &eco->celdas[indice];


    if (celda->organismo.id == id) {

        celda->organismo = eco_organismo_vacio();

        murio = 1;
    }


    ctx_desbloquear_par(
        ctx,
        indice,
        indice
    );


    return murio;
}


/* =========================================================
 * AJUSTES DIRECTOS SOBRE UN ORGANISMO
 *
 * Envejecer y pagar el costo metabolico modifican campos del
 * organismo (edad, energia, hambre) SIN liberar ni ocupar la
 * celda. Deliberadamente no toman cerrojo, y eso es seguro:
 *
 *   - Mientras la celda siga ocupada por este organismo,
 *     ningun otro hilo puede escribirla: moverse, comer o
 *     reproducirse hacia ella exige que este vacia (o que
 *     contenga una presa de otra especie), y la revalidacion
 *     bajo cerrojo lo rechaza.
 *   - Los demas hilos solo leen los campos 'tipo' e 'id' de
 *     esta celda al explorar su vecindad. Son miembros
 *     distintos de los que se escriben aqui, y en C cada
 *     miembro es una posicion de memoria independiente, asi
 *     que no hay carrera.
 *
 * Quitar estos dos cerrojos elimina cuatro operaciones de
 * sincronizacion por organismo y por tick.
 * ========================================================= */

static int envejecer(
    ContextoTick *ctx,
    Posicion pos,
    uint64_t id,
    TipoEspecie tipo_esperado
) {

    Celda *celda;


    celda =
        &ctx->eco->celdas[
            eco_indice_celda(
                ctx->eco,
                pos.fila,
                pos.columna
            )
        ];


    if (
        celda->organismo.id != id ||
        celda->organismo.tipo != tipo_esperado
    ) {

        return 0;
    }


    celda->organismo.edad += 1;

    return 1;
}


static void pagar_metabolismo(
    ContextoTick *ctx,
    Posicion pos,
    uint64_t id,
    int comio,
    int costo
) {

    Celda *celda;


    celda =
        &ctx->eco->celdas[
            eco_indice_celda(
                ctx->eco,
                pos.fila,
                pos.columna
            )
        ];


    if (celda->organismo.id != id) {

        return;
    }


    if (!comio) {

        celda->organismo.ticks_sin_comer += 1;
    }


    celda->organismo.energia -= costo;
}


/* =========================================================
 * PROCESAMIENTO DE UN ORGANISMO
 * ========================================================= */

static void procesar_alga(
    ContextoTick *ctx,
    const EntradaSnapshot *entrada
) {

    Posicion p;


    p = entrada->pos;


    if (
        !envejecer(
            ctx,
            p,
            entrada->id,
            ESPECIE_ALGA
        )
    ) {

        return;
    }


    if (
        morir(
            ctx,
            p.fila,
            p.columna,
            entrada->id,
            EDAD_MAXIMA_ALGA,
            0
        )
    ) {

        return;
    }


    reproducir(
        ctx,
        p.fila,
        p.columna,
        entrada->id,
        ctx->eco->config.prob_reproduccion_alga,
        0,
        0,
        0
    );
}


/*
 * Herbivoros y carnivoros siguen exactamente la misma
 * secuencia; solo cambian la presa y las constantes.
 */
static void procesar_consumidor(
    ContextoTick *ctx,
    const EntradaSnapshot *entrada,
    TipoEspecie tipo,
    TipoEspecie tipo_presa,
    int energia_ganada,
    int costo_metabolico,
    int edad_maxima,
    int ticks_max_sin_comer,
    double probabilidad_reproduccion,
    int energia_minima,
    int comidas_minimas,
    int energia_heredada
) {

    Posicion p;

    int comio;


    p = entrada->pos;


    if (
        !envejecer(
            ctx,
            p,
            entrada->id,
            tipo
        )
    ) {

        return;
    }


    /*
     * Primero intenta comer: si hay una presa vecina se la
     * come y ocupa su celda.
     */
    comio =
        alimentar(
            ctx,
            p.fila,
            p.columna,
            entrada->id,
            tipo_presa,
            energia_ganada,
            &p
        );


    /*
     * Si no encontro comida, explora (o huye).
     */
    if (!comio) {

        mover(
            ctx,
            p.fila,
            p.columna,
            entrada->id,
            &p
        );
    }


    pagar_metabolismo(
        ctx,
        p,
        entrada->id,
        comio,
        costo_metabolico
    );


    if (
        morir(
            ctx,
            p.fila,
            p.columna,
            entrada->id,
            edad_maxima,
            ticks_max_sin_comer
        )
    ) {

        return;
    }


    reproducir(
        ctx,
        p.fila,
        p.columna,
        entrada->id,
        probabilidad_reproduccion,
        energia_minima,
        comidas_minimas,
        energia_heredada
    );
}


static void procesar_caracol(
    ContextoTick *ctx,
    const EntradaSnapshot *entrada
) {

    const Configuracion *config;


    config = &ctx->eco->config;


    procesar_consumidor(
        ctx,
        entrada,
        ESPECIE_CARACOL,
        ESPECIE_ALGA,
        ENERGIA_GANADA_CARACOL_POR_ALGA,
        COSTO_ENERGIA_METABOLICA_CARACOL,
        EDAD_MAXIMA_CARACOL,
        TICKS_MAX_SIN_COMER_CARACOL,
        config->prob_reproduccion_caracol,
        config->energia_inicial_caracol * UMBRAL_REPRODUCCION_MULT_CARACOL,
        COMIDAS_MINIMAS_CARACOL,
        config->energia_inicial_caracol
    );
}


static void procesar_anguila(
    ContextoTick *ctx,
    const EntradaSnapshot *entrada
) {

    const Configuracion *config;


    config = &ctx->eco->config;


    procesar_consumidor(
        ctx,
        entrada,
        ESPECIE_ANGUILA,
        ESPECIE_CARACOL,
        ENERGIA_GANADA_ANGUILA_POR_CARACOL,
        COSTO_ENERGIA_METABOLICA_ANGUILA,
        EDAD_MAXIMA_ANGUILA,
        TICKS_MAX_SIN_COMER_ANGUILA,
        config->prob_reproduccion_anguila,
        config->energia_inicial_anguila * UMBRAL_REPRODUCCION_MULT_ANGUILA,
        COMIDAS_MINIMAS_ANGUILA,
        config->energia_inicial_anguila
    );
}


/* =========================================================
 * BARRIDO DE UNA ESPECIE
 *
 * Aqui esta el paralelismo principal del proyecto: el bucle
 * sobre todos los organismos de una especie se reparte entre
 * los hilos. Se usa schedule(dynamic) porque el trabajo por
 * organismo es irregular (comer, moverse, reproducirse y
 * morir cuestan distinto).
 * ========================================================= */

typedef void (*FuncionProcesar)(
    ContextoTick *ctx,
    const EntradaSnapshot *entrada
);


static void actualizar_especie(
    Ecosistema *eco,
    TipoEspecie tipo,
    FuncionProcesar procesar,
    int paralelo
) {

    size_t total;

    long long conflictos;


    total =
        eco_recolectar_snapshot(
            eco,
            tipo,
            eco->snapshot,
            paralelo
        );


    if (total == 0) {

        return;
    }


    eco_mezclar_snapshot(
        eco,
        eco->snapshot,
        total,
        paralelo
    );


    conflictos = 0;


    if (!paralelo) {

        ContextoTick ctx;

        size_t i;


        ctx_iniciar(
            &ctx,
            eco,
            0,
            0
        );


        for (i = 0; i < total; ++i) {

            procesar(
                &ctx,
                &eco->snapshot[i]
            );
        }


        conflictos = ctx.conflictos;

    }
    else {

#ifdef _OPENMP

        long long limite;


        limite = (long long)total;


#pragma omp parallel reduction(+:conflictos)
        {
            ContextoTick ctx;

            long long i;


            ctx_iniciar(
                &ctx,
                eco,
                1,
                omp_get_thread_num()
            );


#pragma omp for schedule(dynamic, 64) nowait
            for (i = 0; i < limite; ++i) {

                procesar(
                    &ctx,
                    &eco->snapshot[i]
                );
            }


            conflictos += ctx.conflictos;
        }

#endif
    }


    eco->diag.conflictos += conflictos;
}


/* =========================================================
 * UN TICK COMPLETO
 *
 * El orden entre especies se mantiene: productores, luego
 * herbivoros, luego carnivoros. Dentro de cada especie el
 * orden ya era aleatorio en la version secuencial, asi que
 * repartirlo entre hilos no cambia la naturaleza del modelo.
 *
 * Esta funcion no imprime nada: es exactamente el bloque que
 * se cronometra.
 * ========================================================= */

void ecosistema_ejecutar_tick(
    Ecosistema *eco
) {

    int paralelo;


    if (
        eco == NULL ||
        eco->celdas == NULL
    ) {

        return;
    }


    paralelo =
        (eco->config.modo == MODO_PARALELO);


#ifndef _OPENMP

    /*
     * Compilado sin OpenMP: solo existe el camino secuencial.
     */
    paralelo = 0;

#endif


    actualizar_especie(
        eco,
        ESPECIE_ALGA,
        procesar_alga,
        paralelo
    );


    actualizar_especie(
        eco,
        ESPECIE_CARACOL,
        procesar_caracol,
        paralelo
    );


    actualizar_especie(
        eco,
        ESPECIE_ANGUILA,
        procesar_anguila,
        paralelo
    );
}


/* =========================================================
 * CICLO PRINCIPAL
 * ========================================================= */

static void configurar_hilos(
    Ecosistema *eco
) {

#ifdef _OPENMP

    if (eco->config.modo == MODO_PARALELO) {

        if (eco->config.num_hilos > 0) {

            /*
             * Pedir mas hilos que procesadores no acelera
             * nada: solo agrega cambios de contexto. Se
             * permite (sirve para el analisis) pero se avisa.
             */
            if (eco->config.num_hilos > omp_get_num_procs()) {

                fprintf(
                    stderr,
                    "Advertencia: se pidieron %d hilos pero solo hay %d "
                    "procesadores.\n"
                    "             Los tiempos van a empeorar por "
                    "sobresuscripcion.\n",
                    eco->config.num_hilos,
                    omp_get_num_procs()
                );
            }


            omp_set_num_threads(
                eco->config.num_hilos
            );
        }


        eco->hilos_utilizados =
            omp_get_max_threads();

    }
    else {

        eco->hilos_utilizados = 1;
    }

#else

    /*
     * Sin OpenMP no existe la version paralela. Se avisa para
     * que nadie reporte por error tiempos "paralelos" que en
     * realidad son de un solo hilo.
     */
    if (eco->config.modo == MODO_PARALELO) {

        fprintf(
            stderr,
            "Advertencia: este binario se compilo sin OpenMP.\n"
            "             La corrida sera secuencial aunque se "
            "haya pedido --modo paralelo.\n"
        );


        eco->config.modo = MODO_SECUENCIAL;
    }


    eco->hilos_utilizados = 1;

#endif
}


static int toca_reportar(
    const Ecosistema *eco,
    int tick
) {

    int cada;


    cada = eco->config.ticks_por_reporte;


    if (cada < 1) {

        cada = 1;
    }


    if (tick == eco->config.numero_ticks) {

        return 1;
    }


    return (tick % cada) == 0;
}


void ecosistema_simular(
    Ecosistema *eco
) {

    int tick;

    FILE *archivo;

    FILE *consola;


    if (
        eco == NULL ||
        eco->celdas == NULL
    ) {

        return;
    }


    configurar_hilos(eco);


    archivo = NULL;

    if (eco->config.ruta_resultados[0] != '\0') {

        ecosistema_asegurar_directorio(
            eco->config.ruta_resultados
        );


        archivo =
            fopen(
                eco->config.ruta_resultados,
                "w"
            );


        if (archivo == NULL) {

            fprintf(
                stderr,
                "Advertencia: no se pudo escribir '%s'.\n",
                eco->config.ruta_resultados
            );
        }
    }


    consola =
        eco->config.silencioso ? NULL : stdout;


    if (
        consola != NULL &&
        eco->config.mostrar_presentacion
    ) {

        ecosistema_reportar_presentacion(consola);
    }


    if (consola != NULL) {

        ecosistema_reportar_configuracion(eco, consola);
    }


    if (archivo != NULL) {

        ecosistema_reportar_configuracion(eco, archivo);
    }


    /*
     * Tick 0: estado inicial.
     */
    if (consola != NULL) {

        ecosistema_reportar_estado(eco, consola);
    }


    if (archivo != NULL) {

        ecosistema_reportar_estado(eco, archivo);
    }


    eco->tiempo_computo = 0.0;


    for (
        tick = 1;
        tick <= eco->config.numero_ticks;
        ++tick
    ) {

        double inicio;

        double fin;


        eco->tick_actual = tick;


        inicio = ahora_segundos();

        ecosistema_ejecutar_tick(eco);

        fin = ahora_segundos();


        eco->tiempo_computo += fin - inicio;


        if (eco->config.validar_cada_tick) {

            ecosistema_validar_coherencia(eco);
        }


        if (!toca_reportar(eco, tick)) {

            continue;
        }


        if (consola != NULL) {

            ecosistema_reportar_estado(eco, consola);
        }


        if (archivo != NULL) {

            ecosistema_reportar_estado(eco, archivo);
        }
    }


    if (consola != NULL) {

        ecosistema_reportar_resumen(eco, consola);
    }


    if (archivo != NULL) {

        ecosistema_reportar_resumen(eco, archivo);

        fclose(archivo);


        if (consola != NULL) {

            printf(
                "\nResultados escritos en '%s'.\n",
                eco->config.ruta_resultados
            );
        }
    }
}
