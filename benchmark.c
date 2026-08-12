/* =========================================================
 * benchmark.c
 *
 * Barrido de rendimiento: mide la version secuencial y la
 * paralela con distintas cantidades de hilos, calcula
 * speedup y eficiencia y deja el CSV con los resultados.
 *
 * Se cronometra unicamente ecosistema_ejecutar_tick(): sin
 * impresion, sin validacion y sin la inicializacion, que es
 * un costo fijo que no se paraleliza y distorsionaria la
 * comparacion.
 * ========================================================= */

#include "ecosistema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#else
#include <time.h>
#endif


#define REPETICIONES        3
#define TICKS_POR_DEFECTO   20
#define RUTA_CSV_DEFECTO    "resultados/benchmark.csv"


static const int TAMANOS_POR_DEFECTO[] = {
    256,
    1024,
    2048
};

static const int HILOS_A_PROBAR[] = {
    1,
    2,
    4,
    8,
    16
};


typedef struct {

    double tiempo;

    Poblacion poblacion;

    long long conflictos;

} Medicion;


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


static int comparar_dobles(
    const void *a,
    const void *b
) {

    double da;

    double db;


    da = *(const double *)a;

    db = *(const double *)b;


    if (da < db) {

        return -1;
    }


    if (da > db) {

        return 1;
    }


    return 0;
}


/* =========================================================
 * UNA CORRIDA CRONOMETRADA
 * ========================================================= */

static int correr_una_vez(
    const Configuracion *base,
    ModoEjecucion modo,
    int hilos,
    int filas,
    int columnas,
    int ticks,
    Medicion *salida
) {

    Configuracion config;

    Ecosistema eco;

    int tick;

    double inicio;

    double fin;


    config = *base;

    config.filas = filas;

    config.columnas = columnas;

    config.numero_ticks = ticks;

    config.modo = modo;

    config.num_hilos = hilos;

    config.silencioso = 1;

    config.mostrar_presentacion = 0;

    config.mostrar_cuadricula_cada_tick = 0;

    config.validar_cada_tick = 0;

    config.ruta_resultados[0] = '\0';


    configuracion_aplicar_densidades(&config);


    if (!ecosistema_inicializar(&eco, &config)) {

        return 0;
    }


#ifdef _OPENMP

    if (modo == MODO_PARALELO) {

        omp_set_num_threads(hilos);

        eco.hilos_utilizados = hilos;

    }
    else {

        eco.hilos_utilizados = 1;
    }

#endif


    inicio = ahora_segundos();


    for (tick = 1; tick <= ticks; ++tick) {

        eco.tick_actual = tick;

        ecosistema_ejecutar_tick(&eco);
    }


    fin = ahora_segundos();


    salida->tiempo = fin - inicio;

    salida->poblacion = ecosistema_contar_poblacion(&eco);

    salida->conflictos = eco.diag.conflictos;


    ecosistema_liberar(&eco);

    return 1;
}


/*
 * Repite la corrida y se queda con la mediana, que es mucho
 * mas estable que el promedio frente a un pico ocasional del
 * sistema operativo.
 */
static int medir(
    const Configuracion *base,
    ModoEjecucion modo,
    int hilos,
    int filas,
    int columnas,
    int ticks,
    Medicion *salida
) {

    double tiempos[REPETICIONES];

    Medicion actual;

    int r;


    /*
     * Aviso de avance por stderr: un barrido grande puede
     * tardar minutos y sin esto la pantalla se queda muda.
     * Va por stderr para no ensuciar la tabla de stdout.
     */
    fprintf(
        stderr,
        "   ... midiendo %s %dx%d con %d hilo%s\r",
        modo == MODO_PARALELO ? "paralelo  " : "secuencial",
        filas,
        columnas,
        hilos,
        hilos == 1 ? " " : "s"
    );

    fflush(stderr);


    for (r = 0; r < REPETICIONES; ++r) {

        if (
            !correr_una_vez(
                base,
                modo,
                hilos,
                filas,
                columnas,
                ticks,
                &actual
            )
        ) {

            return 0;
        }


        tiempos[r] = actual.tiempo;
    }


    qsort(
        tiempos,
        REPETICIONES,
        sizeof(double),
        comparar_dobles
    );


    *salida = actual;

    salida->tiempo = tiempos[REPETICIONES / 2];


    /*
     * Se limpia la linea de avance para que no quede debajo
     * de la fila de resultados.
     */
    fprintf(
        stderr,
        "\r%60s\r",
        ""
    );

    fflush(stderr);


    return 1;
}


/* =========================================================
 * BARRIDO DE VERIFICACION
 *
 * Repite la simulacion con distintas cantidades de hilos y
 * distintas semillas revisando los invariantes en cada tick.
 *
 * Vive dentro del programa y no en un script de shell para
 * que funcione igual en cualquier sistema, sin depender del
 * interprete de comandos disponible.
 * ========================================================= */

static const int HILOS_A_VERIFICAR[] = {
    2,
    4,
    8,
    16
};

#define SEMILLAS_A_VERIFICAR   10
#define TICKS_VERIFICACION     25
#define LADO_VERIFICACION      256


static int correr_validando(
    const Configuracion *base,
    int hilos,
    uint32_t semilla,
    int saturada,
    Diagnostico *acumulado
) {

    Configuracion config;

    Ecosistema eco;

    int tick;


    config = *base;

    config.filas = LADO_VERIFICACION;

    config.columnas = LADO_VERIFICACION;

    config.numero_ticks = TICKS_VERIFICACION;

    config.modo = MODO_PARALELO;

    config.num_hilos = hilos;

    config.semilla = semilla;

    config.silencioso = 1;

    config.mostrar_presentacion = 0;

    config.mostrar_cuadricula_cada_tick = 0;

    config.validar_cada_tick = 1;

    config.ruta_resultados[0] = '\0';


    configuracion_aplicar_densidades(&config);


    /*
     * El caso saturado deja casi sin celdas libres: es el
     * peor escenario posible para la competencia por celdas.
     */
    if (saturada) {

        long long celdas;


        celdas =
            (long long)config.filas
            * (long long)config.columnas;


        config.algas_iniciales = (int)(celdas * 68 / 100);

        config.caracoles_iniciales = (int)(celdas * 12 / 100);

        config.anguilas_iniciales = (int)(celdas * 5 / 100);
    }


    if (!ecosistema_inicializar(&eco, &config)) {

        return 0;
    }


#ifdef _OPENMP

    omp_set_num_threads(hilos);

#endif


    for (tick = 1; tick <= config.numero_ticks; ++tick) {

        eco.tick_actual = tick;

        ecosistema_ejecutar_tick(&eco);

        ecosistema_validar_coherencia(&eco);
    }


    acumulado->ids_duplicados += eco.diag.ids_duplicados;

    acumulado->celdas_residuales += eco.diag.celdas_residuales;

    acumulado->energias_negativas += eco.diag.energias_negativas;

    acumulado->conflictos += eco.diag.conflictos;


    ecosistema_liberar(&eco);

    return 1;
}


int ecosistema_ejecutar_verificacion(
    const Configuracion *base
) {

    Diagnostico total;

    int procesadores;

    int h;

    int cantidad_hilos;

    int problemas;


    if (base == NULL) {

        return 1;
    }


#ifndef _OPENMP

    fprintf(
        stderr,
        "Error: este binario se compilo sin OpenMP, no hay\n"
        "       version paralela que verificar. Use 'make paralelo'.\n"
    );

    return 1;

#endif


    memset(&total, 0, sizeof(total));


#ifdef _OPENMP
    procesadores = omp_get_num_procs();
#else
    procesadores = 1;
#endif


    cantidad_hilos =
        (int)(
            sizeof(HILOS_A_VERIFICAR)
            /
            sizeof(HILOS_A_VERIFICAR[0])
        );


    printf("\n");
    printf("============================================================\n");
    printf(" VERIFICACION DE RACE CONDITIONS\n");
    printf("============================================================\n");
    printf(" Cuadricula      : %d x %d\n",
        LADO_VERIFICACION, LADO_VERIFICACION);
    printf(" Ticks           : %d\n", TICKS_VERIFICACION);
    printf(" Semillas        : %d\n", SEMILLAS_A_VERIFICAR);
    printf(" Se revisa en cada tick: ids duplicados, celdas vacias\n");
    printf(" con residuos y energias negativas.\n");
    printf("\n");


    for (h = 0; h < cantidad_hilos; ++h) {

        int hilos;

        uint32_t semilla;

        Diagnostico antes;


        hilos = HILOS_A_VERIFICAR[h];


        if (hilos > procesadores) {

            continue;
        }


        antes = total;


        for (
            semilla = 1;
            semilla <= (uint32_t)SEMILLAS_A_VERIFICAR;
            ++semilla
        ) {

            correr_validando(
                base,
                hilos,
                semilla,
                0,
                &total
            );
        }


        /*
         * Una corrida extra con la cuadricula saturada.
         */
        correr_validando(
            base,
            hilos,
            1u,
            1,
            &total
        );


        printf(
            " %2d hilos : %d corridas -> %s\n",
            hilos,
            SEMILLAS_A_VERIFICAR + 1,
            (
                total.ids_duplicados == antes.ids_duplicados &&
                total.celdas_residuales == antes.celdas_residuales &&
                total.energias_negativas == antes.energias_negativas
            )
                ? "OK"
                : "FALLA"
        );
    }


    problemas =
        (int)(
            total.ids_duplicados
            + total.celdas_residuales
            + total.energias_negativas
        );


    printf("\n");
    printf("------------------------------------------------------------\n");
    printf(" Ids duplicados            : %lld\n", total.ids_duplicados);
    printf(" Celdas con residuos       : %lld\n", total.celdas_residuales);
    printf(" Energias negativas        : %lld\n", total.energias_negativas);
    printf(" Acciones por competencia  : %lld\n", total.conflictos);
    printf("------------------------------------------------------------\n");


    if (problemas == 0) {

        printf(" RESULTADO: sin race conditions detectadas.\n");

    }
    else {

        printf(" RESULTADO: FALLA, revisar la sincronizacion.\n");
    }

    printf("============================================================\n");


    return problemas == 0 ? 0 : 2;
}


/* =========================================================
 * BARRIDO COMPLETO
 * ========================================================= */

static int hilos_disponibles(void) {

#ifdef _OPENMP

    return omp_get_num_procs();

#else

    return 1;

#endif
}


int ecosistema_ejecutar_benchmark(
    const Configuracion *base,
    int tamano_explicito
) {

    FILE *csv;

    const char *ruta;

    int tamanos[8];

    int cantidad_tamanos;

    int ticks;

    int procesadores;

    int t;

    int h;

    Medicion calentamiento;


    if (base == NULL) {

        return 1;
    }


#ifndef _OPENMP

    fprintf(
        stderr,
        "Error: este binario se compilo sin OpenMP, no hay\n"
        "       version paralela que medir. Use 'make paralelo'.\n"
    );

    return 1;

#endif


    ruta =
        base->ruta_resultados[0] != '\0'
            ? base->ruta_resultados
            : RUTA_CSV_DEFECTO;


    ticks =
        base->numero_ticks > 0
            ? base->numero_ticks
            : TICKS_POR_DEFECTO;


    /*
     * Si el usuario fijo un tamano, se respeta; si no, se
     * recorre la escalera por defecto.
     */
    if (tamano_explicito) {

        tamanos[0] = base->filas;

        cantidad_tamanos = 1;

    }
    else {

        cantidad_tamanos =
            (int)(
                sizeof(TAMANOS_POR_DEFECTO)
                /
                sizeof(TAMANOS_POR_DEFECTO[0])
            );


        for (t = 0; t < cantidad_tamanos; ++t) {

            tamanos[t] = TAMANOS_POR_DEFECTO[t];
        }
    }


    procesadores = hilos_disponibles();


    ecosistema_asegurar_directorio(ruta);


    csv = fopen(ruta, "w");


    if (csv == NULL) {

        fprintf(
            stderr,
            "Error: no se pudo escribir '%s'.\n",
            ruta
        );

        return 1;
    }


    fprintf(
        csv,
        "modo,hilos,filas,columnas,ticks,tiempo_s,"
        "speedup,eficiencia,conflictos\n"
    );


    printf("\n");
    printf("============================================================\n");
    printf(" BARRIDO DE RENDIMIENTO\n");
    printf("============================================================\n");
    printf(" Procesadores disponibles : %d\n", procesadores);
    printf(" Ticks por corrida        : %d\n", ticks);
    printf(" Repeticiones (mediana)   : %d\n", REPETICIONES);
    printf(" Archivo de salida        : %s\n", ruta);
    printf("\n");


    /*
     * Corrida de calentamiento: la primera medicion de un
     * proceso siempre paga fallos de cache y la creacion del
     * pool de hilos. Se descarta.
     */
    correr_una_vez(
        base,
        MODO_PARALELO,
        2,
        128,
        128,
        3,
        &calentamiento
    );


    for (t = 0; t < cantidad_tamanos; ++t) {

        int filas;

        int columnas;

        Medicion secuencial;

        Medicion base_paralela;

        int tiene_base_paralela;


        filas = tamanos[t];

        columnas =
            tamano_explicito
                ? base->columnas
                : tamanos[t];


        printf(
            "------------------------------------------------------------\n"
        );

        printf(
            " Cuadricula %d x %d\n",
            filas,
            columnas
        );

        printf(
            "------------------------------------------------------------\n"
        );

        printf(
            " %-12s %8s %12s %10s %12s\n",
            "modo",
            "hilos",
            "tiempo (s)",
            "speedup",
            "eficiencia"
        );


        if (
            !medir(
                base,
                MODO_SECUENCIAL,
                1,
                filas,
                columnas,
                ticks,
                &secuencial
            )
        ) {

            fprintf(
                stderr,
                "Error: no se pudo medir %d x %d.\n",
                filas,
                columnas
            );

            continue;
        }


        printf(
            " %-12s %8d %12.4f %10s %12s\n",
            "secuencial",
            1,
            secuencial.tiempo,
            "-",
            "-"
        );


        fprintf(
            csv,
            "secuencial,1,%d,%d,%d,%.6f,1.000,1.000,%lld\n",
            filas,
            columnas,
            ticks,
            secuencial.tiempo,
            secuencial.conflictos
        );


        tiene_base_paralela = 0;

        memset(
            &base_paralela,
            0,
            sizeof(base_paralela)
        );


        for (
            h = 0;
            h < (int)(sizeof(HILOS_A_PROBAR) / sizeof(HILOS_A_PROBAR[0]));
            ++h
        ) {

            int hilos;

            Medicion paralela;

            double speedup;

            double eficiencia;


            hilos = HILOS_A_PROBAR[h];


            if (hilos > procesadores) {

                continue;
            }


            if (
                !medir(
                    base,
                    MODO_PARALELO,
                    hilos,
                    filas,
                    columnas,
                    ticks,
                    &paralela
                )
            ) {

                continue;
            }


            if (!tiene_base_paralela) {

                base_paralela = paralela;

                tiene_base_paralela = 1;
            }


            speedup = 0.0;

            eficiencia = 0.0;


            if (paralela.tiempo > 0.0) {

                speedup =
                    secuencial.tiempo
                    /
                    paralela.tiempo;

                eficiencia =
                    speedup
                    /
                    (double)hilos;
            }


            printf(
                " %-12s %8d %12.4f %10.2f %11.1f%%\n",
                "paralelo",
                hilos,
                paralela.tiempo,
                speedup,
                eficiencia * 100.0
            );


            fprintf(
                csv,
                "paralelo,%d,%d,%d,%d,%.6f,%.4f,%.4f,%lld\n",
                hilos,
                filas,
                columnas,
                ticks,
                paralela.tiempo,
                speedup,
                eficiencia,
                paralela.conflictos
            );
        }


        printf("\n");


        /*
         * Comprobacion ecologica: la version paralela debe
         * terminar con poblaciones del mismo orden que la
         * secuencial. No pueden ser identicas porque el orden
         * de atencion cambia, pero una diferencia enorme
         * delataria un error en las reglas.
         */
        if (tiene_base_paralela) {

            printf(
                " Poblacion final secuencial : P=%d H=%d C=%d\n",
                secuencial.poblacion.algas,
                secuencial.poblacion.caracoles,
                secuencial.poblacion.anguilas
            );

            printf(
                " Poblacion final paralela   : P=%d H=%d C=%d\n",
                base_paralela.poblacion.algas,
                base_paralela.poblacion.caracoles,
                base_paralela.poblacion.anguilas
            );

            printf("\n");
        }
    }


    fclose(csv);


    printf(
        "============================================================\n"
    );

    printf(
        " Resultados guardados en '%s'.\n",
        ruta
    );

    printf(
        "============================================================\n"
    );


    return 0;
}
