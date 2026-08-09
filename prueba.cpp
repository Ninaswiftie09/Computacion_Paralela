#include <iostream>
#include <omp.h>

int main()
{
    #pragma omp parallel
    {
        #pragma omp critical
        {
            std::cout << "Hola desde el hilo "
                      << omp_get_thread_num()
                      << " de "
                      << omp_get_num_threads()
                      << std::endl;
        }
    }

    return 0;
}