CC = gcc
MPICC = mpicc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic
OPENMP = -fopenmp
LDLIBS = -lcrypto

BASE = src/des.c src/comun.c
CABECERAS = src/des.h src/comun.h
BASE_MPI = $(BASE) src/mpi_comun.c
CABECERAS_MPI = $(CABECERAS) src/mpi_comun.h
PROGRAMAS = bin/cifrador bin/secuencial bin/naive bin/ciclico bin/permutado bin/dinamico bin/hibrido

all: $(PROGRAMAS)

bin/cifrador: src/cifrador.c $(BASE) $(CABECERAS) | bin
	$(CC) $(CFLAGS) -o $@ src/cifrador.c $(BASE) $(LDLIBS)

bin/secuencial: src/secuencial.c $(BASE) $(CABECERAS) | bin
	$(CC) $(CFLAGS) -o $@ src/secuencial.c $(BASE) $(LDLIBS)

bin/naive: src/naive.c $(BASE_MPI) $(CABECERAS_MPI) | bin
	$(MPICC) $(CFLAGS) -o $@ src/naive.c $(BASE_MPI) $(LDLIBS)

bin/ciclico: src/ciclico.c $(BASE_MPI) $(CABECERAS_MPI) | bin
	$(MPICC) $(CFLAGS) -o $@ src/ciclico.c $(BASE_MPI) $(LDLIBS)

bin/permutado: src/permutado.c $(BASE_MPI) $(CABECERAS_MPI) | bin
	$(MPICC) $(CFLAGS) -o $@ src/permutado.c $(BASE_MPI) $(LDLIBS)

bin/hibrido: src/hibrido.c $(BASE_MPI) $(CABECERAS_MPI) | bin
	$(MPICC) $(CFLAGS) $(OPENMP) -o $@ src/hibrido.c $(BASE_MPI) $(LDLIBS)

bin/dinamico: src/dinamico.c $(BASE_MPI) $(CABECERAS_MPI) | bin
	$(MPICC) $(CFLAGS) -o $@ src/dinamico.c $(BASE_MPI) $(LDLIBS)

bin:
	mkdir -p bin

clean:
	rm -rf bin

.PHONY: all clean
