#ifndef ECOSISTEMA_H
#define ECOSISTEMA_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define MAX_NOMBRE_ECOSISTEMA 64
#define MAX_RUTA_ARCHIVO      512
#define MAX_VECINOS 8

/*
 * Simulacion de ecosistema con OpenMP
 *
 * Equivalencias ecologicas del proyecto:
 *
 *   ALGA     -> planta / productor
 *   CARACOL  -> herbivoro / consumidor primario
 *   ANGUILA  -> carnivoro / depredador
 *
 * El mismo conjunto de reglas se ejecuta en dos modos:
 * secuencial y paralelo con OpenMP. La unica diferencia es
 * quien resuelve los conflictos de escritura sobre la
 * cuadricula (ver reglas.c).
 */

/* TIPOS DEL ECOSISTEMA */

typedef enum {
    ESPECIE_VACIA = 0,
    ESPECIE_ALGA,
    ESPECIE_CARACOL,
    ESPECIE_ANGUILA
} TipoEspecie;


/* Modo de ejecucion del ciclo de ticks */
typedef enum {
    MODO_SECUENCIAL = 0,
    MODO_PARALELO
} ModoEjecucion;


/*
 * Estilo de simbolos de la cuadricula.
 *
 *   FONDO_BIKINI -> A / G / E / .
 *   PDF          -> P / H / C / .
 */
typedef enum {
    SIMBOLOS_FONDO_BIKINI = 0,
    SIMBOLOS_PDF
} EstiloSimbolos;


/* Posicion dentro de la cuadricula */
typedef struct {
    int fila;
    int columna;
} Posicion;

typedef struct {
    uint64_t id;

    TipoEspecie tipo;

    int energia;
    int edad;
    int comidas;
    int ticks_sin_comer;
} Organismo;


/* Cada posicion de la cuadricula contiene un organismo */
typedef struct {
    Organismo organismo;
} Celda;


/*
 * Entrada del snapshot que se toma al inicio de cada fase.
 *
 * Guardar el id ademas de la posicion es lo que garantiza que
 * cada organismo se procese exactamente una vez por tick: si
 * otro organismo de la misma especie llego a esa celda mientras
 * tanto, el id ya no coincide y la entrada se descarta.
 */
typedef struct {
    Posicion pos;

    uint64_t id;
} EntradaSnapshot;


/* Conteo general de habitantes del ecosistema */
typedef struct {
    int algas;
    int caracoles;
    int anguilas;
    int vacias;
} Poblacion;


/*
 * Contadores de diagnostico.
 *
 * Los tres primeros deben quedar SIEMPRE en cero: si alguno
 * sube, hubo una race condition. El ultimo no es un error,
 * mide cuantas acciones se perdieron por competencia entre
 * organismos que querian la misma celda.
 */
typedef struct {
    long long ids_duplicados;
    long long celdas_residuales;
    long long energias_negativas;

    long long conflictos;
} Diagnostico;


/* CONFIGURACION */

typedef struct {
    char nombre[MAX_NOMBRE_ECOSISTEMA];

    int filas;
    int columnas;

    int algas_iniciales;
    int caracoles_iniciales;
    int anguilas_iniciales;

    /*
     * Densidades como fraccion de la cuadricula. Permiten
     * escalar el tamano del problema para las mediciones sin
     * tener que recalcular las cantidades a mano.
     */
    double densidad_algas;
    double densidad_caracoles;
    double densidad_anguilas;

    int numero_ticks;

    double prob_reproduccion_alga;
    double prob_reproduccion_caracol;
    double prob_reproduccion_anguila;

    int energia_inicial_caracol;
    int energia_inicial_anguila;

    uint32_t semilla;

    /* EJECUCION */

    ModoEjecucion modo;

    /* 0 = dejar que OpenMP decida */
    int num_hilos;

    /* SALIDA */

    int mostrar_presentacion;

    int mostrar_cuadricula_cada_tick;

    /* 1 = sin salida por pantalla (para medir tiempos) */
    int silencioso;

    /* Se reporta 1 de cada N ticks */
    int ticks_por_reporte;

    EstiloSimbolos simbolos;

    /* Cadena vacia = no escribir archivo de resultados */
    char ruta_resultados[MAX_RUTA_ARCHIVO];

    /* Cadena vacia = no exportar la corrida */
    char ruta_exportacion[MAX_RUTA_ARCHIVO];

    /* DIAGNOSTICO */

    int validar_cada_tick;

} Configuracion;


/*  ECOSISTEMA */

typedef struct {

    Configuracion config;

    Celda *celdas;

    uint64_t siguiente_id;

    int tick_actual;

    uint32_t rng_estado;

    /*
     * Buffer reutilizable para los snapshots por especie. Se
     * reserva una sola vez en ecosistema_inicializar() en vez
     * de una vez por especie por tick.
     */
    EntradaSnapshot *snapshot;

    /*
     * Un cerrojo por celda (omp_lock_t *). Se declara como
     * void * para no arrastrar <omp.h> hasta este header;
     * queda en NULL cuando se compila sin OpenMP.
     */
    void *cerrojos;

    /* Hilos realmente utilizados en la ultima corrida */
    int hilos_utilizados;

    /* Segundos de computo puro, sin contar la impresion */
    double tiempo_computo;

    Diagnostico diag;

} Ecosistema;


/* CONFIGURACION */

Configuracion configuracion_fondo_bikini(void);

/*
 * Recalcula las poblaciones iniciales a partir de las
 * densidades y del tamano actual de la cuadricula.
 */
void configuracion_aplicar_densidades(
    Configuracion *config
);


/* CICLO DE VIDA DEL ECOSISTEMA */

int ecosistema_inicializar(
    Ecosistema *eco,
    const Configuracion *config
);

void ecosistema_liberar(
    Ecosistema *eco
);

void ecosistema_simular(
    Ecosistema *eco
);

/*
 * Avanza un unico tick. Es el bloque que se cronometra: no
 * imprime nada.
 */
void ecosistema_ejecutar_tick(
    Ecosistema *eco
);


/* ACCESO A CELDAS */

Celda *ecosistema_acceder_celda(
    Ecosistema *eco,
    int fila,
    int columna
);

const Celda *ecosistema_consultar_celda(
    const Ecosistema *eco,
    int fila,
    int columna
);


/* =========================================================
 * VECINOS
 *
 * Se usa la vecindad de Moore:
 *
 * X X X
 * X O X
 * X X X
 *
 * Una celda puede tener hasta 8 vecinos.
 * ========================================================= */

int ecosistema_obtener_vecinos(
    const Ecosistema *eco,
    int fila,
    int columna,
    Posicion salida[MAX_VECINOS]
);

int ecosistema_obtener_vecinos_vacios(
    const Ecosistema *eco,
    int fila,
    int columna,
    Posicion salida[MAX_VECINOS]
);

int ecosistema_obtener_vecinos_tipo(
    const Ecosistema *eco,
    int fila,
    int columna,
    TipoEspecie tipo,
    Posicion salida[MAX_VECINOS]
);


/*  NUMEROS ALEATORIOS */

int ecosistema_aleatorio_entero(
    Ecosistema *eco,
    int minimo,
    int maximo
);

double ecosistema_aleatorio_01(
    Ecosistema *eco
);

int ecosistema_ocurre(
    Ecosistema *eco,
    double probabilidad
);


/* ESTADISTICAS Y DIAGNOSTICO */

Poblacion ecosistema_contar_poblacion(
    const Ecosistema *eco
);

/*
 * Revisa invariantes de la cuadricula y acumula los hallazgos
 * en eco->diag. Devuelve la cantidad de problemas encontrados.
 */
long long ecosistema_validar_coherencia(
    Ecosistema *eco
);


/* =========================================================
 * VISUALIZACION
 *
 * Las funciones reportar_* escriben en cualquier FILE*, de
 * modo que la misma salida sirve para la consola y para el
 * archivo de resultados. Las imprimir_* son atajos a stdout.
 * ========================================================= */

void ecosistema_reportar_presentacion(
    FILE *destino
);

void ecosistema_reportar_configuracion(
    const Ecosistema *eco,
    FILE *destino
);

void ecosistema_reportar_cuadricula(
    const Ecosistema *eco,
    FILE *destino
);

void ecosistema_reportar_estado(
    const Ecosistema *eco,
    FILE *destino
);

void ecosistema_reportar_resumen(
    const Ecosistema *eco,
    FILE *destino
);

void ecosistema_imprimir_presentacion(void);

void ecosistema_imprimir_configuracion(
    const Ecosistema *eco
);

void ecosistema_imprimir_cuadricula(
    const Ecosistema *eco
);

void ecosistema_imprimir_estado(
    const Ecosistema *eco
);


/* =========================================================
 * LINEA DE COMANDOS
 * ========================================================= */

/*
 * Devuelve 1 si la configuracion quedo lista para ejecutar,
 * 0 si hubo un error de uso y -1 si se pidio la ayuda (en
 * cuyo caso el programa debe terminar sin simular).
 */
int configuracion_desde_argumentos(
    int argc,
    char **argv,
    Configuracion *salida
);

void configuracion_imprimir_ayuda(
    const char *nombre_programa
);

/*
 * Crea el directorio que contiene a ruta_archivo si hace
 * falta. Devuelve 1 si la ruta quedo utilizable.
 */
int ecosistema_asegurar_directorio(
    const char *ruta_archivo
);


/* =========================================================
 * MEDICION DE RENDIMIENTO
 * ========================================================= */

/*
 * Barrido de tiempos: compara secuencial contra paralelo con
 * distintas cantidades de hilos y escribe el CSV de
 * resultados. Devuelve 0 si todo salio bien.
 */
int ecosistema_ejecutar_benchmark(
    const Configuracion *base,
    int tamano_explicito
);

/*
 * Barrido de verificacion: repite la simulacion con varias
 * cantidades de hilos y varias semillas, revisando los
 * invariantes en cada tick. Devuelve 0 si todo quedo limpio y
 * 2 si detecto alguna race condition.
 */
int ecosistema_ejecutar_verificacion(
    const Configuracion *base
);


/* =========================================================
 * EXPORTACION PARA EL VISOR
 * ========================================================= */

/*
 * Corre la simulacion y escribe la secuencia de cuadros que
 * reproduce visor.py. Se detiene antes de tiempo si el
 * ecosistema se extingue.
 */
int ecosistema_exportar_corrida(
    const Configuracion *base,
    const char *ruta
);


#endif
