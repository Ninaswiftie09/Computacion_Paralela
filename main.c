#include <stdio.h>

#include "ecosistema.h"


int main(void) {

    Configuracion config;

    Ecosistema fondo_bikini;


    config =
        configuracion_fondo_bikini();

    if (
        !ecosistema_inicializar(
            &fondo_bikini,
            &config
        )
    ) {

        fprintf(
            stderr,
            "No fue posible iniciar Fondo de Bikini.\n"
        );

        return 1;
    }


    ecosistema_simular(
        &fondo_bikini
    );

    ecosistema_liberar(
        &fondo_bikini
    );


    return 0;
}