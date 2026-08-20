# Makefile -- Detonacion, screensaver paralelo con OpenMP
# Proyecto #1, Computacion Paralela y Distribuida (UVG, Seccion 20)

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Isrc
SDL_FLAGS := $(shell pkg-config --cflags --libs sdl2 2>/dev/null || echo "-lSDL2")

SRC_COMUN := src/cli.cpp src/palette.cpp src/particles.cpp
OBJ_DIR   := build

.PHONY: all seq par clean

all: seq

# --- Version secuencial (avance actual) ---
seq: detonacion_seq

detonacion_seq: src/main.cpp $(SRC_COMUN) src/*.h
	$(CXX) $(CXXFLAGS) src/main.cpp $(SRC_COMUN) -o detonacion_seq $(SDL_FLAGS)

# --- Version paralela (OpenMP) -- siguiente entrega ---
par: detonacion_par

detonacion_par: src/main.cpp $(SRC_COMUN) src/*.h
	$(CXX) $(CXXFLAGS) -fopenmp src/main.cpp $(SRC_COMUN) -o detonacion_par $(SDL_FLAGS)

clean:
	rm -f detonacion_seq detonacion_par
	rm -rf $(OBJ_DIR)
