/* =========================================================
 * salida.c
 *
 * Todo lo que se muestra al usuario. Las funciones
 * reportar_* escriben en cualquier FILE*, de modo que la
 * consola y el archivo de resultados comparten exactamente el
 * mismo formato.
 * ========================================================= */

#include "interno.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define CREAR_DIRECTORIO(ruta) _mkdir(ruta)
#else
#include <sys/stat.h>
#define CREAR_DIRECTORIO(ruta) mkdir((ruta), 0777)
#endif


/*
 * A partir de este tamano la cuadricula ya no cabe en una
 * pantalla: se muestra solo una ventana representativa.
 */
#define CELDAS_IMPRESION_COMPLETA   4096
#define FILAS_VENTANA               24
#define COLUMNAS_VENTANA            64


/* =========================================================
 * DIRECTORIOS
 * ========================================================= */

int ecosistema_asegurar_directorio(
    const char *ruta_archivo
) {

    char carpeta[MAX_RUTA_ARCHIVO];

    size_t largo;

    size_t i;


    if (ruta_archivo == NULL) {

        return 0;
    }


    largo = strlen(ruta_archivo);

    if (largo >= MAX_RUTA_ARCHIVO) {

        return 0;
    }


    /*
     * Se recorre la ruta creando cada nivel intermedio, no
     * solo el ultimo: con "a/b/c.txt" hay que crear "a" antes
     * de poder crear "a/b".
     *
     * Si un nivel ya existe, CREAR_DIRECTORIO falla y no pasa
     * nada: lo que importa es que exista al terminar.
     */
    for (i = 0; i < largo; ++i) {

        if (
            ruta_archivo[i] != '/' &&
            ruta_archivo[i] != '\\'
        ) {

            continue;
        }


        /*
         * Una barra en la posicion cero es una ruta absoluta
         * de estilo POSIX: no hay nada que crear ahi.
         */
        if (i == 0) {

            continue;
        }


        memcpy(carpeta, ruta_archivo, i);

        carpeta[i] = '\0';


        CREAR_DIRECTORIO(carpeta);
    }


    return 1;
}


/* =========================================================
 * PRESENTACION
 * ========================================================= */

void ecosistema_reportar_presentacion(
    FILE *destino
) {

    if (destino == NULL) {

        return;
    }


    fprintf(destino, "\n");
    fprintf(destino, "============================================================\n");
    fprintf(destino, "          SIMULACION DE ECOSISTEMA CON OpenMP\n");
    fprintf(destino, "============================================================\n");

    fprintf(destino, "\n");
    fprintf(destino, " ESPECIES\n");
    fprintf(destino, " -----------------------------------------------------------\n");
    fprintf(destino, " P  Planta      Productor\n");
    fprintf(destino, " H  Herbivoro   Consumidor primario\n");
    fprintf(destino, " C  Carnivoro   Depredador\n");
    fprintf(destino, " .  Vacio       Espacio disponible\n");

    fprintf(destino, "\n");
    fprintf(destino, " Cadena alimenticia:\n");
    fprintf(destino, "\n");
    fprintf(destino, "     PLANTA  -->  HERBIVORO  -->  CARNIVORO\n");

    fprintf(destino, "\n");
    fprintf(destino, "============================================================\n");
}


/* =========================================================
 * CONFIGURACION EN PANTALLA
 * ========================================================= */

void ecosistema_reportar_configuracion(
    const Ecosistema *eco,
    FILE *destino
) {

    if (
        eco == NULL ||
        destino == NULL
    ) {

        return;
    }


    fprintf(
        destino,
        "\nCONFIGURACION DEL ECOSISTEMA\n"
    );

    fprintf(
        destino,
        "------------------------------------------------------------\n"
    );


    fprintf(
        destino,
        "Nombre                 : %s\n",
        eco->config.nombre
    );


    fprintf(
        destino,
        "Cuadricula             : %d x %d  (%lld celdas)\n",
        eco->config.filas,
        eco->config.columnas,
        (long long)eco->config.filas
        * (long long)eco->config.columnas
    );


    fprintf(
        destino,
        "Plantas iniciales      : %d\n",
        eco->config.algas_iniciales
    );


    fprintf(
        destino,
        "Herbivoros iniciales   : %d\n",
        eco->config.caracoles_iniciales
    );


    fprintf(
        destino,
        "Carnivoros iniciales   : %d\n",
        eco->config.anguilas_iniciales
    );


    fprintf(
        destino,
        "Numero de ticks        : %d\n",
        eco->config.numero_ticks
    );


    fprintf(
        destino,
        "Semilla                : %u\n",
        eco->config.semilla
    );


    fprintf(
        destino,
        "Prob. repr. plantas    : %.0f%%\n",
        eco->config.prob_reproduccion_alga
        *
        100.0
    );


    fprintf(
        destino,
        "Prob. repr. herbivoros : %.0f%%\n",
        eco->config.prob_reproduccion_caracol
        *
        100.0
    );


    fprintf(
        destino,
        "Prob. repr. carnivoros : %.0f%%\n",
        eco->config.prob_reproduccion_anguila
        *
        100.0
    );


    fprintf(
        destino,
        "Modo de ejecucion      : %s\n",
        eco->config.modo == MODO_PARALELO
            ? "paralelo (OpenMP)"
            : "secuencial"
    );


    if (eco->config.modo == MODO_PARALELO) {

        fprintf(
            destino,
            "Hilos solicitados      : %s\n",
            eco->config.num_hilos > 0
                ? "fijos por parametro"
                : "los que decida OpenMP"
        );
    }


    fprintf(
        destino,
        "------------------------------------------------------------\n"
    );
}


/* =========================================================
 * IMPRESION DE CUADRICULA
 * ========================================================= */

void ecosistema_reportar_cuadricula(
    const Ecosistema *eco,
    FILE *destino
) {

    int fila;

    int columna;

    int filas_mostradas;

    int columnas_mostradas;

    int recortada;


    if (
        eco == NULL ||
        eco->celdas == NULL ||
        destino == NULL
    ) {

        return;
    }


    filas_mostradas = eco->config.filas;

    columnas_mostradas = eco->config.columnas;

    recortada = 0;


    /*
     * Una cuadricula grande no se puede volcar entera: se
     * muestra la esquina superior izquierda como muestra
     * representativa.
     */
    if (
        (long long)eco->config.filas * (long long)eco->config.columnas
        >
        (long long)CELDAS_IMPRESION_COMPLETA
    ) {

        if (filas_mostradas > FILAS_VENTANA) {

            filas_mostradas = FILAS_VENTANA;

            recortada = 1;
        }


        if (columnas_mostradas > COLUMNAS_VENTANA) {

            columnas_mostradas = COLUMNAS_VENTANA;

            recortada = 1;
        }
    }


    if (recortada) {

        fprintf(
            destino,
            "\n(ventana de %d x %d sobre una cuadricula de %d x %d)\n",
            filas_mostradas,
            columnas_mostradas,
            eco->config.filas,
            eco->config.columnas
        );
    }


    /*
     * Numeros de columnas.
     */
    fprintf(destino, "\n     ");


    for (
        columna = 0;
        columna < columnas_mostradas;
        ++columna
    ) {

        fprintf(
            destino,
            "%d ",
            columna % 10
        );
    }


    fprintf(destino, "\n");


    fprintf(destino, "    +");


    for (
        columna = 0;
        columna < columnas_mostradas;
        ++columna
    ) {

        fprintf(destino, "--");
    }


    fprintf(destino, "+\n");


    /*
     * Contenido.
     */
    for (
        fila = 0;
        fila < filas_mostradas;
        ++fila
    ) {

        fprintf(
            destino,
            "%3d |",
            fila
        );


        for (
            columna = 0;
            columna < columnas_mostradas;
            ++columna
        ) {

            const Celda *celda;


            celda =
                ecosistema_consultar_celda(
                    eco,
                    fila,
                    columna
                );


            fprintf(
                destino,
                "%c ",
                eco_simbolo_especie(
                    celda->organismo.tipo,
                    eco->config.simbolos
                )
            );
        }


        fprintf(destino, "|\n");
    }


    fprintf(destino, "    +");


    for (
        columna = 0;
        columna < columnas_mostradas;
        ++columna
    ) {

        fprintf(destino, "--");
    }


    fprintf(destino, "+\n");
}


/* =========================================================
 * ESTADO DEL ECOSISTEMA
 * ========================================================= */

void ecosistema_reportar_estado(
    const Ecosistema *eco,
    FILE *destino
) {

    Poblacion poblacion;


    if (
        eco == NULL ||
        destino == NULL
    ) {

        return;
    }


    poblacion =
        ecosistema_contar_poblacion(
            eco
        );


    fprintf(
        destino,
        "\n============================================================\n"
    );


    if (eco->tick_actual == 0) {

        fprintf(
            destino,
            " ESTADO INICIAL - %s\n",
            eco->config.nombre
        );

    }
    else {

        fprintf(
            destino,
            " TICK %d - %s\n",
            eco->tick_actual,
            eco->config.nombre
        );
    }


    fprintf(
        destino,
        "============================================================\n"
    );


    fprintf(
        destino,
        " Plantas                 : %d\n",
        poblacion.algas
    );


    fprintf(
        destino,
        " Herbivoros              : %d\n",
        poblacion.caracoles
    );


    fprintf(
        destino,
        " Carnivoros              : %d\n",
        poblacion.anguilas
    );


    fprintf(
        destino,
        " Celdas vacias           : %d\n",
        poblacion.vacias
    );


    if (
        eco->config.mostrar_cuadricula_cada_tick
    ) {

        fprintf(
            destino,
            "\n Distribucion  (%s):\n",
            eco->config.simbolos == SIMBOLOS_PDF
                ? "P plantas, H herbivoros, C carnivoros"
                : "A algas, G caracoles, E anguilas"
        );

        ecosistema_reportar_cuadricula(
            eco,
            destino
        );
    }
}


/* =========================================================
 * RESUMEN FINAL
 * ========================================================= */

void ecosistema_reportar_resumen(
    const Ecosistema *eco,
    FILE *destino
) {

    double ticks_por_segundo;

    double celdas_por_segundo;


    if (
        eco == NULL ||
        destino == NULL
    ) {

        return;
    }


    ticks_por_segundo = 0.0;

    celdas_por_segundo = 0.0;


    if (eco->tiempo_computo > 0.0) {

        ticks_por_segundo =
            (double)eco->config.numero_ticks
            /
            eco->tiempo_computo;


        celdas_por_segundo =
            ticks_por_segundo
            *
            (double)eco->config.filas
            *
            (double)eco->config.columnas;
    }


    fprintf(
        destino,
        "\n============================================================\n"
    );

    fprintf(
        destino,
        " SIMULACION FINALIZADA\n"
    );

    fprintf(
        destino,
        "============================================================\n"
    );


    fprintf(
        destino,
        " Ticks completados       : %d\n",
        eco->config.numero_ticks
    );


    fprintf(
        destino,
        " Modo                    : %s\n",
        eco->config.modo == MODO_PARALELO
            ? "paralelo (OpenMP)"
            : "secuencial"
    );


    fprintf(
        destino,
        " Hilos utilizados        : %d\n",
        eco->hilos_utilizados
    );


    fprintf(
        destino,
        " Tiempo de computo       : %.6f s\n",
        eco->tiempo_computo
    );


    fprintf(
        destino,
        " Ticks por segundo       : %.2f\n",
        ticks_por_segundo
    );


    fprintf(
        destino,
        " Celdas por segundo      : %.3e\n",
        celdas_por_segundo
    );


    /*
     * No es un error: es la competencia por recursos. Cuenta
     * las veces que un organismo perdio su accion porque otro
     * ocupo la celda primero.
     */
    fprintf(
        destino,
        " Acciones por competencia: %lld\n",
        eco->diag.conflictos
    );


    if (eco->config.validar_cada_tick) {

        fprintf(
            destino,
            "\n VALIDACION DE INVARIANTES\n"
        );

        fprintf(
            destino,
            " Ids duplicados          : %lld\n",
            eco->diag.ids_duplicados
        );

        fprintf(
            destino,
            " Celdas con residuos     : %lld\n",
            eco->diag.celdas_residuales
        );

        fprintf(
            destino,
            " Energias negativas      : %lld\n",
            eco->diag.energias_negativas
        );


        if (
            eco->diag.ids_duplicados == 0 &&
            eco->diag.celdas_residuales == 0 &&
            eco->diag.energias_negativas == 0
        ) {

            fprintf(
                destino,
                " Resultado               : OK, sin race conditions\n"
            );

        }
        else {

            fprintf(
                destino,
                " Resultado               : FALLA, revisar sincronizacion\n"
            );
        }
    }


    fprintf(
        destino,
        "============================================================\n"
    );
}


/* =========================================================
 * ATAJOS A CONSOLA
 * ========================================================= */

void ecosistema_imprimir_presentacion(void) {

    ecosistema_reportar_presentacion(stdout);
}


void ecosistema_imprimir_configuracion(
    const Ecosistema *eco
) {

    ecosistema_reportar_configuracion(eco, stdout);
}


void ecosistema_imprimir_cuadricula(
    const Ecosistema *eco
) {

    ecosistema_reportar_cuadricula(eco, stdout);
}


void ecosistema_imprimir_estado(
    const Ecosistema *eco
) {

    ecosistema_reportar_estado(eco, stdout);
}
