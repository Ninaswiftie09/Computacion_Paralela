CC=gcc
MPICC=mpicc
CFLAGS=-O2 -Wall -Wextra
all: des_tool secuencial ciclico
des_tool: des_tool.c des_lib.c des_lib.h
	$(CC) $(CFLAGS) -o $@ des_tool.c des_lib.c -lcrypto
secuencial: secuencial.c des_lib.c des_lib.h
	$(CC) $(CFLAGS) -o $@ secuencial.c des_lib.c -lcrypto
ciclico: ciclico.c des_lib.c des_lib.h
	$(MPICC) $(CFLAGS) -o $@ ciclico.c des_lib.c -lcrypto
clean:
	rm -f des_tool secuencial ciclico *.bin
