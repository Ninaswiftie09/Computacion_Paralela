# Makefile -- Screensaver de N cuerpos con gravedad
# Proyecto #1, Computacion Paralela y Distribuida (UVG, Seccion 20)
#
# Ambos binarios salen de LOS MISMOS fuentes: los #pragma omp son inertes si no
# se compila con -fopenmp. Asi la version secuencial y la paralela ejecutan
# exactamente la misma fisica, y comparar sus tiempos es honesto.
#
#   make        -> compila las dos versiones
#   make seq    -> solo nbody_seq (sin OpenMP)
#   make par    -> solo nbody_par (con OpenMP)

CXX      := g++
CXXFLAGS := -std=c++17 -O3 -march=native -Wall -Wextra -Isrc

# Banderas de SDL2. Se le quitan tres cosas a pkg-config:
#   -Dmain=SDL_main y -lSDL2main  -> usamos SDL_MAIN_HANDLED, no el main de SDL
#   -mwindows                     -> queremos la consola visible para los FPS
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null | sed 's/-Dmain=SDL_main//g')
SDL_LIBS   := $(shell pkg-config --libs   sdl2 2>/dev/null | sed -e 's/-lSDL2main//g' -e 's/-mwindows//g')

# Respaldo por si pkg-config no esta instalado.
ifeq ($(strip $(SDL_LIBS)),)
  SDL_CFLAGS := -I/ucrt64/include/SDL2
  SDL_LIBS   := -L/ucrt64/lib -lmingw32 -lSDL2
endif

FUENTES := src/main.cpp src/cli.cpp src/nbody.cpp src/palette.cpp src/render.cpp
CABECERAS := $(wildcard src/*.h)

.PHONY: all seq par clean

all: seq par

seq: nbody_seq
par: nbody_par

nbody_seq: $(FUENTES) $(CABECERAS)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $(FUENTES) -o $@ $(SDL_LIBS)

nbody_par: $(FUENTES) $(CABECERAS)
	$(CXX) $(CXXFLAGS) -fopenmp $(SDL_CFLAGS) $(FUENTES) -o $@ $(SDL_LIBS)

clean:
	rm -f nbody_seq nbody_seq.exe nbody_par nbody_par.exe
