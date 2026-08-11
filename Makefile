# =========================================================
# Simulacion de ecosistema con OpenMP
#
#   make                -> compila las dos versiones
#   make paralelo       -> solo la version con OpenMP
#   make secuencial     -> solo la version de un hilo
#   make resultados     -> genera el archivo de resultados
#   make bench          -> barrido de rendimiento (CSV)
#   make verificar      -> busca race conditions
#   make clean          -> borra binarios y salidas
#
# Las recetas no usan bucles ni utilidades de shell, para que
# funcionen igual con cmd.exe, PowerShell o una shell POSIX.
# =========================================================

CC       = gcc
CFLAGS   = -O2 -Wall -Wextra -std=c11
OMPFLAGS = -fopenmp

FUENTES   = ecosistema.c reglas.c salida.c cli.c benchmark.c main.c
CABECERAS = ecosistema.h interno.h

ifeq ($(OS),Windows_NT)
    EXE = .exe
    RUN =
    BORRAR = -del /Q /F
else
    EXE =
    RUN = ./
    BORRAR = -rm -f
endif

PARALELO   = ecosistema_paralelo$(EXE)
SECUENCIAL = ecosistema_secuencial$(EXE)

DIR_RESULTADOS = resultados


.PHONY: all paralelo secuencial resultados bench verificar clean

all: paralelo secuencial

paralelo: $(PARALELO)

secuencial: $(SECUENCIAL)


# La version paralela es el binario principal: incluye tambien
# el camino secuencial, seleccionable con --modo secuencial.
$(PARALELO): $(FUENTES) $(CABECERAS)
	$(CC) $(CFLAGS) $(OMPFLAGS) $(FUENTES) -o $@


# Version compilada sin OpenMP. Sirve para demostrar que el
# codigo no depende de la biblioteca y como referencia limpia
# de tiempos, sin el costo del runtime de hilos.
$(SECUENCIAL): $(FUENTES) $(CABECERAS)
	$(CC) $(CFLAGS) $(FUENTES) -o $@


# Archivo de resultados pedido en los entregables: estado del
# ecosistema en varios puntos del tiempo.
resultados: $(PARALELO)
	$(RUN)$(PARALELO) --ticks 20 --cada 5 --simbolos pdf --sin-presentacion --quiet --salida $(DIR_RESULTADOS)/resultados.txt
	$(RUN)$(PARALELO) --filas 64 --columnas 64 --ticks 30 --cada 10 --modo paralelo --hilos 8 --simbolos pdf --sin-presentacion --quiet --validar --salida $(DIR_RESULTADOS)/resultados_paralelo.txt
	@echo Resultados generados en $(DIR_RESULTADOS)


# Los 20 ticks son los que documenta docs/RESULTADOS.md: hay que
# fijarlos aqui para que la medicion sea reproducible.
bench: $(PARALELO)
	$(RUN)$(PARALELO) --bench --ticks 20 --salida $(DIR_RESULTADOS)/benchmark.csv


# Busca race conditions: repite la simulacion con 2, 4, 8 y 16
# hilos y varias semillas, revisando los invariantes en cada
# tick. Devuelve codigo 2 si encuentra algo roto.
verificar: $(PARALELO)
	$(RUN)$(PARALELO) --verificar-todo


clean:
	$(BORRAR) $(PARALELO)
	$(BORRAR) $(SECUENCIAL)
