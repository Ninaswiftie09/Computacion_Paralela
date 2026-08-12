/* =========================================================
 * main.c
 *
 * Punto de entrada. Lee los parametros, decide si toca una
 * simulacion normal o el barrido de rendimiento, y devuelve
 * un codigo de salida util para automatizar las pruebas:
 *
 *   0 -> todo bien
 *   1 -> error de uso o de inicializacion
 *   2 -> la validacion encontro invariantes rotos
 * ========================================================= */

#include <stdio.h>
#include <string.h>

#include "ecosistema.h"


static int contiene_opcion(
    int argc,
    char **argv,
    const char *buscada
) {

    int i;


    for (i = 1; i < argc; ++i) {

        if (strcmp(argv[i], buscada) == 0) {

            return 1;
        }
    }


    return 0;
}


int main(int argc, char **argv) {

    Configuracion config;

    Ecosistema fondo_bikini;

    int estado;

    int salida;


    estado =
        configuracion_desde_argumentos(
            argc,
            argv,
            &config
        );


    /*
     * Se pidio la ayuda: no hay nada que simular.
     */
    if (estado < 0) {

        return 0;
    }


    if (estado == 0) {

        return 1;
    }


    if (contiene_opcion(argc, argv, "--bench")) {

        return
            ecosistema_ejecutar_benchmark(
                &config,
                contiene_opcion(argc, argv, "--filas") ||
                contiene_opcion(argc, argv, "--columnas")
            );
    }


    if (contiene_opcion(argc, argv, "--verificar-todo")) {

        return
            ecosistema_ejecutar_verificacion(
                &config
            );
    }


    if (config.ruta_exportacion[0] != '\0') {

        return
            ecosistema_exportar_corrida(
                &config,
                config.ruta_exportacion
            );
    }


    if (
        !ecosistema_inicializar(
            &fondo_bikini,
            &config
        )
    ) {

        fprintf(
            stderr,
            "No fue posible iniciar la simulacion.\n"
        );

        return 1;
    }


    ecosistema_simular(
        &fondo_bikini
    );


    salida = 0;


    /*
     * Si se pidio validar, el codigo de salida refleja el
     * resultado para poder encadenarlo en un script.
     */
    if (
        config.validar_cada_tick &&
        (
            fondo_bikini.diag.ids_duplicados != 0 ||
            fondo_bikini.diag.celdas_residuales != 0 ||
            fondo_bikini.diag.energias_negativas != 0
        )
    ) {

        salida = 2;
    }


    ecosistema_liberar(
        &fondo_bikini
    );


    return salida;
}
