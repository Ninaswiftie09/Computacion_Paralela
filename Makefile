CC = gcc
MPICC = mpicc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic
LDLIBS = -lcrypto

BASE = src/des.c src/comun.c
CABECERAS = src/des.h src/comun.h
PROGRAMAS = bin/cifrador bin/secuencial bin/ciclico

all: $(PROGRAMAS)

bin/cifrador: src/cifrador.c $(BASE) $(CABECERAS) | bin
	$(CC) $(CFLAGS) -o $@ src/cifrador.c $(BASE) $(LDLIBS)

bin/secuencial: src/secuencial.c $(BASE) $(CABECERAS) | bin
	$(CC) $(CFLAGS) -o $@ src/secuencial.c $(BASE) $(LDLIBS)

bin/ciclico: src/ciclico.c $(BASE) $(CABECERAS) | bin
	$(MPICC) $(CFLAGS) -o $@ src/ciclico.c $(BASE) $(LDLIBS)

bin:
	mkdir -p bin

clean:
	rm -rf bin

.PHONY: all clean
