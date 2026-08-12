/* =========================================================
 * visor.c
 *
 * Exporta la corrida completa a un archivo de texto compacto
 * que despues reproduce visor.py con pygame.
 *
 * Formato del archivo:
 *
 *   ECO 1
 *   filas <n>
 *   columnas <n>
 *   semilla <n>
 *   modo <secuencial|paralelo>
 *   hilos <n>
 *   datos
 *   <una linea por cuadro, un caracter por celda>
 *
 * Los caracteres son P, H, C y punto para celda vacia. Cada
 * cuadro se escribe conforme se produce, de modo que no hay
 * que guardar toda la historia en memoria.
 *
 * La cantidad de cuadros no va en la cabecera a proposito: la
 * corrida puede terminar antes por extincion, y contar lineas
 * al leer es mas simple y mas robusto que volver atras a
 * reescribir la cabecera.
 * ========================================================= */

#include "interno.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif


static void escribir_cuadro(
    FILE *f,
    const Ecosistema *eco
) {

    size_t capacidad;

    size_t i;


    capacidad = eco_capacidad(eco);


    for (i = 0; i < capacidad; ++i) {

        char c;


        switch (eco->celdas[i].organismo.tipo) {

            case ESPECIE_ALGA:      c = 'P'; break;

            case ESPECIE_CARACOL:   c = 'H'; break;

            case ESPECIE_ANGUILA:   c = 'C'; break;

            default:                c = '.'; break;
        }


        fputc(c, f);
    }


    fputc('\n', f);
}


int ecosistema_exportar_corrida(
    const Configuracion *base,
    const char *ruta
) {

    Configuracion config;

    Ecosistema eco;

    FILE *f;

    int tick;

    int extinto;

    int cuadros;


    if (
        base == NULL ||
        ruta == NULL
    ) {

        return 1;
    }


    config = *base;

    config.silencioso = 1;

    config.mostrar_presentacion = 0;

    config.validar_cada_tick = 0;

    config.ruta_resultados[0] = '\0';


    if (!ecosistema_inicializar(&eco, &config)) {

        return 1;
    }


#ifdef _OPENMP

    if (config.modo == MODO_PARALELO && config.num_hilos > 0) {

        omp_set_num_threads(config.num_hilos);
    }

#endif


    ecosistema_asegurar_directorio(ruta);


    f = fopen(ruta, "wb");


    if (f == NULL) {

        fprintf(
            stderr,
            "Error: no se pudo escribir '%s'.\n",
            ruta
        );

        ecosistema_liberar(&eco);

        return 1;
    }


    fprintf(f, "ECO 1\n");

    fprintf(f, "filas %d\n", config.filas);

    fprintf(f, "columnas %d\n", config.columnas);

    fprintf(f, "semilla %u\n", config.semilla);

    fprintf(
        f,
        "modo %s\n",
        config.modo == MODO_PARALELO ? "paralelo" : "secuencial"
    );

    fprintf(
        f,
        "hilos %d\n",
        config.modo == MODO_PARALELO
            ? (config.num_hilos > 0 ? config.num_hilos : 0)
            : 1
    );

    fprintf(f, "datos\n");


    escribir_cuadro(f, &eco);

    cuadros = 1;

    extinto = 0;


    for (
        tick = 1;
        tick <= config.numero_ticks;
        ++tick
    ) {

        Poblacion p;


        eco.tick_actual = tick;

        ecosistema_ejecutar_tick(&eco);

        escribir_cuadro(f, &eco);

        ++cuadros;


        p = ecosistema_contar_poblacion(&eco);


        if (p.algas + p.caracoles + p.anguilas == 0) {

            extinto = 1;

            break;
        }
    }


    fclose(f);


    printf(
        "Corrida exportada a '%s' (%d cuadros).\n",
        ruta,
        cuadros
    );


    if (extinto) {

        printf(
            "El ecosistema se extinguio en el tick %d.\n",
            tick
        );
    }


    ecosistema_liberar(&eco);

    return 0;
}
