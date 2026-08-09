#ifndef ECOSISTEMA_H
#define ECOSISTEMA_H

#include <stdint.h>
#include <stddef.h>

#define MAX_NOMBRE_ECOSISTEMA 64
#define MAX_VECINOS 8

/*
 * Fondo de Bikini - Motor base secuencial
 *
 * Equivalencias ecologicas del proyecto:
 *
 *   ALGA     -> planta / productor
 *   CARACOL  -> herbivoro / consumidor primario
 *   ANGUILA  -> carnivoro / depredador
 */

/* TIPOS DEL ECOSISTEMA */

typedef enum {
    ESPECIE_VACIA = 0,
    ESPECIE_ALGA,
    ESPECIE_CARACOL,
    ESPECIE_ANGUILA
} TipoEspecie;


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


/* Conteo general de habitantes del ecosistema */
typedef struct {
    int algas;
    int caracoles;
    int anguilas;
    int vacias;
} Poblacion;


/* CONFIGURACION */

typedef struct {
    char nombre[MAX_NOMBRE_ECOSISTEMA];

    int filas;
    int columnas;

    int algas_iniciales;
    int caracoles_iniciales;
    int anguilas_iniciales;

    int numero_ticks;

    double prob_reproduccion_alga;
    double prob_reproduccion_caracol;
    double prob_reproduccion_anguila;

    int energia_inicial_caracol;
    int energia_inicial_anguila;

    uint32_t semilla;

    int mostrar_cuadricula_cada_tick;

} Configuracion;


/*  ECOSISTEMA */

typedef struct {

    Configuracion config;

    Celda *celdas;

    uint64_t siguiente_id;

    int tick_actual;

    uint32_t rng_estado;

} Ecosistema;


/* CONFIGURACION */

Configuracion configuracion_fondo_bikini(void);


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


/* ESTADISTICAS Y VISUALIZACION  */

Poblacion ecosistema_contar_poblacion(
    const Ecosistema *eco
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


#endif