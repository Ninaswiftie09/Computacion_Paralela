#include "ecosistema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* =========================================================
 * UTILIDADES INTERNAS
 * ========================================================= */

static size_t indice_celda(
    const Ecosistema *eco,
    int fila,
    int columna
) {
    return
        (size_t)fila * (size_t)eco->config.columnas
        + (size_t)columna;
}


/*
 * Verifica que una posicion exista dentro
 * de Fondo de Bikini.
 */
static int posicion_valida(
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


/*
 * Crea el contenido correspondiente a una
 * celda que no contiene ningun organismo.
 */
static Organismo organismo_vacio(void) {

    Organismo organismo;

    organismo.id = 0;

    organismo.tipo = ESPECIE_VACIA;

    organismo.energia = 0;

    organismo.edad = 0;

    organismo.comidas = 0;

    organismo.ticks_sin_comer = 0;

    return organismo;
}


/*
 * Crea un nuevo organismo y le asigna
 * automaticamente un ID unico.
 */
static Organismo crear_organismo(
    Ecosistema *eco,
    TipoEspecie tipo
) {

    Organismo organismo;

    organismo = organismo_vacio();

    organismo.id = eco->siguiente_id++;

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
 * ========================================================= */

/*
 * Se utiliza xorshift32.
 *
 * Es un generador pequeno y rapido que permite utilizar
 * una semilla fija.
 *
 * Gracias a esto, si usamos la misma semilla,
 * obtenemos exactamente la misma distribucion inicial.
 *
 * Esto sera especialmente util para comparar pruebas.
 */
static uint32_t siguiente_aleatorio(
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


/* =========================================================
 * MEZCLA DE POSICIONES
 * ========================================================= */

/*
 * Fisher-Yates Shuffle
 *
 * Creamos todos los indices de la cuadricula y los
 * revolvemos.
 *
 * Esto permite colocar organismos aleatoriamente
 * sin que dos organismos ocupen la misma celda.
 */
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
            (size_t)siguiente_aleatorio(eco)
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

static char simbolo_especie(
    TipoEspecie tipo
) {

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


    capacidad =
        (size_t)eco->config.filas
        *
        (size_t)eco->config.columnas;


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
            crear_organismo(
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
            crear_organismo(
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
            crear_organismo(
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
        "Fondo de Bikini"
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
     * Cantidad de ticks.
     */
    config.numero_ticks = 10;


    /*
     * Probabilidades.
     *
     * Todavia no se aplican.
     * Persona 2 implementara las reglas.
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
     * 1 = mostrar cuadricula
     * 0 = no mostrarla
     */
    config.mostrar_cuadricula_cada_tick = 1;


    return config;
}


/* =========================================================
 * INICIALIZACION
 * ========================================================= */

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


    eco->siguiente_id = 1;

    eco->tick_actual = 0;


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
            organismo_vacio();
    }


    /*
     * Colocamos los organismos iniciales.
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
        !posicion_valida(
            eco,
            fila,
            columna
        )
    ) {

        return NULL;
    }


    return &eco->celdas[
        indice_celda(
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
        !posicion_valida(
            eco,
            fila,
            columna
        )
    ) {

        return NULL;
    }


    return &eco->celdas[
        indice_celda(
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
            siguiente_aleatorio(eco)
            %
            rango
        );
}


double ecosistema_aleatorio_01(
    Ecosistema *eco
) {

    return
        (double)siguiente_aleatorio(eco)
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
        !posicion_valida(
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
                posicion_valida(
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
 * CONTEO DE POBLACION
 * ========================================================= */

Poblacion ecosistema_contar_poblacion(
    const Ecosistema *eco
) {

    Poblacion poblacion;

    size_t capacidad;

    size_t i;


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


    capacidad =
        (size_t)eco->config.filas
        *
        (size_t)eco->config.columnas;


    for (
        i = 0;
        i < capacidad;
        ++i
    ) {

        switch (
            eco->celdas[i].organismo.tipo
        ) {

            case ESPECIE_ALGA:

                ++poblacion.algas;

                break;


            case ESPECIE_CARACOL:

                ++poblacion.caracoles;

                break;


            case ESPECIE_ANGUILA:

                ++poblacion.anguilas;

                break;


            case ESPECIE_VACIA:

            default:

                ++poblacion.vacias;

                break;
        }
    }


    return poblacion;
}


/* =========================================================
 * PRESENTACION
 * ========================================================= */
static void imprimir_bob_esponja(void)
{
    FILE *archivo;
    char linea[4096];

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    archivo = fopen("bob.txt", "r");

    if (archivo == NULL)
    {
        printf("\n");
        printf("          FONDO DE BIKINI\n");
        printf("  Bob Esponja no pudo salir de su piña :(\n");
        printf("\n");

        return;
    }

    while (fgets(linea, sizeof(linea), archivo) != NULL)
    {
        printf("%s", linea);
    }

    fclose(archivo);

    printf("\n");
}


void ecosistema_imprimir_presentacion(void)
{
    imprimir_bob_esponja();

    printf("\n");
    printf("============================================================\n");
    printf("       FONDO DE BIKINI - SIMULACION DE ECOSISTEMA\n");
    printf("              Motor secuencial en lenguaje C\n");
    printf("============================================================\n");

    printf("\n");
    printf(" Bob Esponja te da la bienvenida al experimento.\n");

    printf("\n");
    printf(" HABITANTES DEL ECOSISTEMA\n");
    printf(" -----------------------------------------------------------\n");
    printf(" A  Alga       -> Planta / productor\n");
    printf(" G  Gary       -> Caracol herbivoro\n");
    printf(" E  Anguila    -> Carnivoro / depredador\n");
    printf(" .  Agua       -> Espacio disponible\n");

    printf("\n");
    printf(" Cadena alimenticia:\n");
    printf("\n");
    printf("          ALGA  --->  GARY  --->  ANGUILA\n");
    printf("        productor   herbivoro    carnivoro\n");

    printf("\n");
    printf("============================================================\n");
}


/* =========================================================
 * CONFIGURACION EN PANTALLA
 * ========================================================= */

void ecosistema_imprimir_configuracion(
    const Ecosistema *eco
) {

    if (eco == NULL) {

        return;
    }


    printf(
        "\nCONFIGURACION DEL ECOSISTEMA\n"
    );

    printf(
        "------------------------------------------------------------\n"
    );


    printf(
        "Nombre                 : %s\n",
        eco->config.nombre
    );


    printf(
        "Cuadricula             : %d x %d\n",
        eco->config.filas,
        eco->config.columnas
    );


    printf(
        "Algas iniciales        : %d\n",
        eco->config.algas_iniciales
    );


    printf(
        "Caracoles iniciales    : %d\n",
        eco->config.caracoles_iniciales
    );


    printf(
        "Anguilas iniciales     : %d\n",
        eco->config.anguilas_iniciales
    );


    printf(
        "Numero de ticks        : %d\n",
        eco->config.numero_ticks
    );


    printf(
        "Semilla                : %u\n",
        eco->config.semilla
    );


    printf(
        "Prob. reproduccion A   : %.0f%%\n",
        eco->config.prob_reproduccion_alga
        *
        100.0
    );


    printf(
        "Prob. reproduccion G   : %.0f%%\n",
        eco->config.prob_reproduccion_caracol
        *
        100.0
    );


    printf(
        "Prob. reproduccion E   : %.0f%%\n",
        eco->config.prob_reproduccion_anguila
        *
        100.0
    );


    printf(
        "------------------------------------------------------------\n"
    );
}


/* =========================================================
 * IMPRESION DE CUADRICULA
 * ========================================================= */

void ecosistema_imprimir_cuadricula(
    const Ecosistema *eco
) {

    int fila;

    int columna;


    if (
        eco == NULL ||
        eco->celdas == NULL
    ) {

        return;
    }


    /*
     * Numeros de columnas.
     */
    printf("\n     ");


    for (
        columna = 0;
        columna < eco->config.columnas;
        ++columna
    ) {

        printf(
            "%d ",
            columna % 10
        );
    }


    printf("\n");


    printf("    +");


    for (
        columna = 0;
        columna < eco->config.columnas;
        ++columna
    ) {

        printf("--");
    }


    printf("+\n");


    /*
     * Contenido.
     */
    for (
        fila = 0;
        fila < eco->config.filas;
        ++fila
    ) {

        printf(
            "%3d |",
            fila
        );


        for (
            columna = 0;
            columna < eco->config.columnas;
            ++columna
        ) {

            const Celda *celda;


            celda =
                ecosistema_consultar_celda(
                    eco,
                    fila,
                    columna
                );


            printf(
                "%c ",
                simbolo_especie(
                    celda->organismo.tipo
                )
            );
        }


        printf("|\n");
    }


    printf("    +");


    for (
        columna = 0;
        columna < eco->config.columnas;
        ++columna
    ) {

        printf("--");
    }


    printf("+\n");
}


/* =========================================================
 * ESTADO DEL ECOSISTEMA
 * ========================================================= */

void ecosistema_imprimir_estado(
    const Ecosistema *eco
) {

    Poblacion poblacion;


    if (eco == NULL) {

        return;
    }


    poblacion =
        ecosistema_contar_poblacion(
            eco
        );


    printf(
        "\n============================================================\n"
    );


    if (eco->tick_actual == 0) {

        printf(
            " ESTADO INICIAL - %s\n",
            eco->config.nombre
        );

    }
    else {

        printf(
            " TICK %d - %s\n",
            eco->tick_actual,
            eco->config.nombre
        );
    }


    printf(
        "============================================================\n"
    );


    printf(
        " Algas      (plantas)    : %d\n",
        poblacion.algas
    );


    printf(
        " Caracoles  (herbivoros) : %d\n",
        poblacion.caracoles
    );


    printf(
        " Anguilas   (carnivoros) : %d\n",
        poblacion.anguilas
    );


    printf(
        " Celdas vacias           : %d\n",
        poblacion.vacias
    );


    if (
        eco->config.mostrar_cuadricula_cada_tick
    ) {

        ecosistema_imprimir_cuadricula(
            eco
        );
    }
}


static void actualizar_algas(
    Ecosistema *eco
) {

    /*
     * Evita warning de parametro no utilizado
     * mientras esta funcion aun no tiene reglas.
     */
    (void)eco;


    /*
     * PERSONA 2:
     *
     * Aqui se implementara:
     *
     * - crecimiento de algas;
     * - expansion;
     * - reproduccion segun probabilidad;
     * - busqueda de vecinos vacios;
     * - reglas de muerte que se definan.
     */
}


static void actualizar_caracoles(
    Ecosistema *eco
) {

    (void)eco;


    /*
     * PERSONA 2:
     *
     * Aqui se implementara:
     *
     * - buscar algas cercanas;
     * - movimiento;
     * - consumir algas;
     * - energia;
     * - hambre;
     * - edad;
     * - reproduccion;
     * - muerte.
     */
}


static void actualizar_anguilas(
    Ecosistema *eco
) {

    (void)eco;


    /*
     * PERSONA 2:
     *
     * Aqui se implementara:
     *
     * - buscar caracoles cercanos;
     * - movimiento;
     * - depredacion;
     * - energia;
     * - hambre;
     * - edad;
     * - reproduccion;
     * - muerte.
     */
}


/* =========================================================
 * EJECUTAR UN TICK
 * ========================================================= */

static void ejecutar_tick(
    Ecosistema *eco
) {



    actualizar_algas(
        eco
    );


    actualizar_caracoles(
        eco
    );


    actualizar_anguilas(
        eco
    );
}


/* =========================================================
 * CICLO PRINCIPAL SECUENCIAL
 * ========================================================= */

void ecosistema_simular(
    Ecosistema *eco
) {

    int tick;


    if (
        eco == NULL ||
        eco->celdas == NULL
    ) {

        return;
    }


    ecosistema_imprimir_presentacion();


    ecosistema_imprimir_configuracion(
        eco
    );


    /*
     * Tick 0:
     * estado inicial.
     */
    ecosistema_imprimir_estado(
        eco
    );


    /*
     * Ciclo secuencial principal.
     */
    for (
        tick = 1;
        tick <= eco->config.numero_ticks;
        ++tick
    ) {

        eco->tick_actual =
            tick;


        ejecutar_tick(
            eco
        );


        ecosistema_imprimir_estado(
            eco
        );
    }


    printf(
        "\n============================================================\n"
    );

    printf(
        " SIMULACION FINALIZADA\n"
    );

    printf(
        "============================================================\n"
    );

    printf(
        " Se completaron %d ticks de forma secuencial.\n",
        eco->config.numero_ticks
    );

    printf(
        " Motor base listo jeje.\n"
    );

    printf(
        "============================================================\n"
    );
}