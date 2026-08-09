#include <stdio.h>
#include <omp.h>

int main() {
    // Obtener el número de hilos especificado por la variable de entorno OMP_NUM_THREADS
    int num_threads = omp_get_max_threads();
    printf("Número de hilos disponibles: %d\n", num_threads);

#pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        printf("Hola desde el hilo %d\n", thread_id);
    }

    return 0;
}
