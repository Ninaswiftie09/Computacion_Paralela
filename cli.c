/* =========================================================
 * cli.c
 *
 * Lectura de los parametros de la simulacion desde la linea
 * de comandos. Sin esto no se puede variar el tamano del
 * problema y por lo tanto no se puede medir el rendimiento.
 * ========================================================= */

#include "ecosistema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int leer_entero(
    const char *texto,
    const char *opcion,
    int *destino
) {

    char *resto;

    long valor;


    if (texto == NULL) {

        fprintf(
            stderr,
            "Error: %s necesita un valor.\n",
            opcion
        );

        return 0;
    }


    valor = strtol(texto, &resto, 10);


    if (
        resto == texto ||
        *resto != '\0'
    ) {

        fprintf(
            stderr,
            "Error: '%s' no es un numero valido para %s.\n",
            texto,
            opcion
        );

        return 0;
    }


    *destino = (int)valor;

    return 1;
}


static int leer_entero_sin_signo(
    const char *texto,
    const char *opcion,
    uint32_t *destino
) {

    char *resto;

    unsigned long valor;


    if (texto == NULL) {

        fprintf(
            stderr,
            "Error: %s necesita un valor.\n",
            opcion
        );

        return 0;
    }


    valor = strtoul(texto, &resto, 10);


    if (
        resto == texto ||
        *resto != '\0'
    ) {

        fprintf(
            stderr,
            "Error: '%s' no es un numero valido para %s.\n",
            texto,
            opcion
        );

        return 0;
    }


    *destino = (uint32_t)valor;

    return 1;
}


static int leer_probabilidad(
    const char *texto,
    const char *opcion,
    double *destino
) {

    char *resto;

    double valor;


    if (texto == NULL) {

        fprintf(
            stderr,
            "Error: %s necesita un valor.\n",
            opcion
        );

        return 0;
    }


    valor = strtod(texto, &resto);


    if (
        resto == texto ||
        *resto != '\0' ||
        valor < 0.0 ||
        valor > 1.0
    ) {

        fprintf(
            stderr,
            "Error: %s espera una probabilidad entre 0 y 1 "
            "(se recibio '%s').\n",
            opcion,
            texto
        );

        return 0;
    }


    *destino = valor;

    return 1;
}


void configuracion_imprimir_ayuda(
    const char *nombre_programa
) {

    printf(
        "\nSimulacion de ecosistema con OpenMP\n\n"
    );

    printf(
        "Uso: %s [opciones]\n\n",
        nombre_programa != NULL ? nombre_programa : "ecosistema"
    );

    printf("CUADRICULA Y POBLACION\n");
    printf("  --filas N              Filas de la cuadricula\n");
    printf("  --columnas N           Columnas de la cuadricula\n");
    printf("  --algas N              Cantidad inicial de plantas\n");
    printf("  --caracoles N          Cantidad inicial de herbivoros\n");
    printf("  --anguilas N           Cantidad inicial de carnivoros\n");
    printf("\n");
    printf("  Si se da solo un lado, la cuadricula queda cuadrada.\n");
    printf("  Si se cambia el tamano sin fijar cantidades, las\n");
    printf("  poblaciones se recalculan manteniendo la densidad.\n\n");

    printf("SIMULACION\n");
    printf("  --ticks N              Cantidad de ticks a simular\n");
    printf("  --semilla N            Semilla del generador aleatorio\n");
    printf("  --prob-alga P          Probabilidad de reproduccion (0 a 1)\n");
    printf("  --prob-caracol P       Probabilidad de reproduccion (0 a 1)\n");
    printf("  --prob-anguila P       Probabilidad de reproduccion (0 a 1)\n\n");

    printf("EJECUCION\n");
    printf("  --modo secuencial      Ejecuta en un solo hilo\n");
    printf("  --modo paralelo        Ejecuta con OpenMP\n");
    printf("  --hilos N              Cantidad de hilos (0 = automatico)\n\n");

    printf("SALIDA\n");
    printf("  --quiet                No imprime nada por pantalla\n");
    printf("  --sin-cuadricula       Solo poblaciones, sin el mapa\n");
    printf("  --sin-presentacion     Omite la portada\n");
    printf("  --cada N               Reporta 1 de cada N ticks\n");
    printf("  --simbolos pdf|bikini  P/H/C o A/G/E\n");
    printf("  --salida RUTA          Escribe el archivo de resultados\n");
    printf("  --exportar RUTA        Exporta la corrida para visor.py\n\n");

    printf("DIAGNOSTICO\n");
    printf("  --validar              Revisa invariantes en cada tick\n");
    printf("  --bench                Corre el barrido de rendimiento\n");
    printf("  --verificar-todo       Busca race conditions con varios hilos\n");
    printf("  --ayuda                Muestra esta ayuda\n\n");

    printf("EJEMPLOS\n");
    printf("  Corrida por defecto:\n");
    printf("    ecosistema\n\n");
    printf("  Cuadricula grande en paralelo, sin imprimir:\n");
    printf("    ecosistema --filas 1024 --columnas 1024 --ticks 50 \\\n");
    printf("               --modo paralelo --hilos 8 --quiet\n\n");
    printf("  Archivo de resultados con simbolos del enunciado:\n");
    printf("    ecosistema --simbolos pdf --salida resultados/resultados.txt\n\n");
}


int configuracion_desde_argumentos(
    int argc,
    char **argv,
    Configuracion *salida
) {

    int i;

    int tamano_cambiado;

    int poblacion_explicita;

    int filas_dada;

    int columnas_dada;


    if (salida == NULL) {

        return 0;
    }


    *salida = configuracion_fondo_bikini();

    tamano_cambiado = 0;

    poblacion_explicita = 0;

    filas_dada = 0;

    columnas_dada = 0;


    for (i = 1; i < argc; ++i) {

        const char *opcion;

        const char *valor;


        opcion = argv[i];

        valor = (i + 1 < argc) ? argv[i + 1] : NULL;


        if (
            strcmp(opcion, "--ayuda") == 0 ||
            strcmp(opcion, "--help") == 0 ||
            strcmp(opcion, "-h") == 0
        ) {

            configuracion_imprimir_ayuda(argv[0]);

            return -1;
        }


        if (strcmp(opcion, "--filas") == 0) {

            if (!leer_entero(valor, opcion, &salida->filas)) {

                return 0;
            }

            tamano_cambiado = 1;

            filas_dada = 1;

            ++i;

            continue;
        }


        if (strcmp(opcion, "--columnas") == 0) {

            if (!leer_entero(valor, opcion, &salida->columnas)) {

                return 0;
            }

            tamano_cambiado = 1;

            columnas_dada = 1;

            ++i;

            continue;
        }


        if (strcmp(opcion, "--algas") == 0) {

            if (!leer_entero(valor, opcion, &salida->algas_iniciales)) {

                return 0;
            }

            poblacion_explicita = 1;

            ++i;

            continue;
        }


        if (strcmp(opcion, "--caracoles") == 0) {

            if (!leer_entero(valor, opcion, &salida->caracoles_iniciales)) {

                return 0;
            }

            poblacion_explicita = 1;

            ++i;

            continue;
        }


        if (strcmp(opcion, "--anguilas") == 0) {

            if (!leer_entero(valor, opcion, &salida->anguilas_iniciales)) {

                return 0;
            }

            poblacion_explicita = 1;

            ++i;

            continue;
        }


        if (strcmp(opcion, "--ticks") == 0) {

            if (!leer_entero(valor, opcion, &salida->numero_ticks)) {

                return 0;
            }

            ++i;

            continue;
        }


        if (strcmp(opcion, "--semilla") == 0) {

            if (!leer_entero_sin_signo(valor, opcion, &salida->semilla)) {

                return 0;
            }

            ++i;

            continue;
        }


        if (strcmp(opcion, "--prob-alga") == 0) {

            if (
                !leer_probabilidad(
                    valor,
                    opcion,
                    &salida->prob_reproduccion_alga
                )
            ) {

                return 0;
            }

            ++i;

            continue;
        }


        if (strcmp(opcion, "--prob-caracol") == 0) {

            if (
                !leer_probabilidad(
                    valor,
                    opcion,
                    &salida->prob_reproduccion_caracol
                )
            ) {

                return 0;
            }

            ++i;

            continue;
        }


        if (strcmp(opcion, "--prob-anguila") == 0) {

            if (
                !leer_probabilidad(
                    valor,
                    opcion,
                    &salida->prob_reproduccion_anguila
                )
            ) {

                return 0;
            }

            ++i;

            continue;
        }


        if (strcmp(opcion, "--modo") == 0) {

            if (valor == NULL) {

                fprintf(
                    stderr,
                    "Error: --modo necesita 'secuencial' o 'paralelo'.\n"
                );

                return 0;
            }


            if (strcmp(valor, "secuencial") == 0) {

                salida->modo = MODO_SECUENCIAL;

            }
            else if (strcmp(valor, "paralelo") == 0) {

                salida->modo = MODO_PARALELO;

            }
            else {

                fprintf(
                    stderr,
                    "Error: modo '%s' desconocido "
                    "(use secuencial o paralelo).\n",
                    valor
                );

                return 0;
            }

            ++i;

            continue;
        }


        if (strcmp(opcion, "--hilos") == 0) {

            if (!leer_entero(valor, opcion, &salida->num_hilos)) {

                return 0;
            }


            if (salida->num_hilos < 0) {

                fprintf(
                    stderr,
                    "Error: --hilos no puede ser negativo.\n"
                );

                return 0;
            }


            /*
             * Pedir hilos implica querer la version paralela.
             */
            if (salida->num_hilos > 0) {

                salida->modo = MODO_PARALELO;
            }

            ++i;

            continue;
        }


        if (strcmp(opcion, "--quiet") == 0) {

            salida->silencioso = 1;

            continue;
        }


        if (strcmp(opcion, "--sin-cuadricula") == 0) {

            salida->mostrar_cuadricula_cada_tick = 0;

            continue;
        }


        if (strcmp(opcion, "--sin-presentacion") == 0) {

            salida->mostrar_presentacion = 0;

            continue;
        }


        if (strcmp(opcion, "--cada") == 0) {

            if (!leer_entero(valor, opcion, &salida->ticks_por_reporte)) {

                return 0;
            }


            if (salida->ticks_por_reporte < 1) {

                salida->ticks_por_reporte = 1;
            }

            ++i;

            continue;
        }


        if (strcmp(opcion, "--simbolos") == 0) {

            if (valor == NULL) {

                fprintf(
                    stderr,
                    "Error: --simbolos necesita 'bikini' o 'pdf'.\n"
                );

                return 0;
            }


            if (strcmp(valor, "bikini") == 0) {

                salida->simbolos = SIMBOLOS_FONDO_BIKINI;

            }
            else if (strcmp(valor, "pdf") == 0) {

                salida->simbolos = SIMBOLOS_PDF;

            }
            else {

                fprintf(
                    stderr,
                    "Error: estilo '%s' desconocido "
                    "(use bikini o pdf).\n",
                    valor
                );

                return 0;
            }

            ++i;

            continue;
        }


        if (strcmp(opcion, "--salida") == 0) {

            if (valor == NULL) {

                fprintf(
                    stderr,
                    "Error: --salida necesita una ruta.\n"
                );

                return 0;
            }


            if (strlen(valor) >= MAX_RUTA_ARCHIVO) {

                fprintf(
                    stderr,
                    "Error: la ruta de salida es demasiado larga.\n"
                );

                return 0;
            }


            strcpy(salida->ruta_resultados, valor);

            ++i;

            continue;
        }


        if (strcmp(opcion, "--validar") == 0) {

            salida->validar_cada_tick = 1;

            continue;
        }


        if (strcmp(opcion, "--exportar") == 0) {

            if (valor == NULL) {

                fprintf(
                    stderr,
                    "Error: --exportar necesita una ruta.\n"
                );

                return 0;
            }


            if (strlen(valor) >= MAX_RUTA_ARCHIVO) {

                fprintf(
                    stderr,
                    "Error: la ruta del visor es demasiado larga.\n"
                );

                return 0;
            }


            strcpy(salida->ruta_exportacion, valor);

            ++i;

            continue;
        }


        if (
            strcmp(opcion, "--bench") == 0 ||
            strcmp(opcion, "--verificar-todo") == 0
        ) {

            /*
             * Los interpreta main(); aqui solo se aceptan
             * para que no se reporten como desconocidos.
             */
            continue;
        }


        fprintf(
            stderr,
            "Error: opcion desconocida '%s'. "
            "Use --ayuda para ver las disponibles.\n",
            opcion
        );

        return 0;
    }


    /*
     * Dar solo un lado significa querer una cuadricula
     * cuadrada. Sin esto, "--filas 512" dejaria las 24
     * columnas por defecto y produciria una franja de 512x24,
     * que casi nunca es lo que se busca.
     */
    if (
        filas_dada &&
        !columnas_dada
    ) {

        salida->columnas = salida->filas;

    }
    else if (
        columnas_dada &&
        !filas_dada
    ) {

        salida->filas = salida->columnas;
    }


    /*
     * Si se cambio el tamano de la cuadricula pero no las
     * cantidades, se mantiene la densidad original para que
     * la ecologia sea comparable entre tamanos.
     */
    if (
        tamano_cambiado &&
        !poblacion_explicita
    ) {

        configuracion_aplicar_densidades(salida);
    }


    return 1;
}
