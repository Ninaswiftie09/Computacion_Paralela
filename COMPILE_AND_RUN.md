# Compilación y Ejecución - N-Bodies Screensaver

## Prerequisitos

- **Compilador**: `g++` (TDM-GCC o MSYS2 ucrt64)
- **SDL2**: Librerías de desarrollo
- **pkg-config**: Para encontrar flags de compilación
- **OpenMP**: Para paralelización (incluido en GCC)

### Instalación en MSYS2/UCRT64

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-pkgconf
```

## Compilar

### Opción 1: Usar `make` (Linux/MSYS2)

```bash
make          # Compila ambas versiones (seq + par)
make seq      # Solo versión secuencial
make par      # Solo versión paralela
make clean    # Limpia binarios
```

### Opción 2: PowerShell (Windows con TDM-GCC)

```powershell
# Compilar versión secuencial
g++ -std=c++17 -O3 -march=native -Wall -Wextra -Isrc -Wno-unknown-pragmas `
  -I/ucrt64/include/SDL2 `
  src/main.cpp src/cli.cpp src/nbody.cpp src/palette.cpp src/render.cpp src/hud.cpp `
  -o nbody_seq.exe -L/ucrt64/lib -lmingw32 -lSDL2

# Compilar versión paralela
g++ -std=c++17 -O3 -march=native -Wall -Wextra -Isrc -fopenmp `
  -I/ucrt64/include/SDL2 `
  src/main.cpp src/cli.cpp src/nbody.cpp src/palette.cpp src/render.cpp src/hud.cpp `
  -o nbody_par.exe -L/ucrt64/lib -lmingw32 -lSDL2
```

## Ejecutar

### Versión Secuencial

```bash
./nbody_seq.exe
./nbody_seq.exe -n 8000 -m colision -t 1
./nbody_seq.exe --help
```

### Versión Paralela

```bash
./nbody_par.exe
./nbody_par.exe -n 8000 -m colision -t 8    # 8 hilos
./nbody_par.exe -t 0                         # Automático (recomendado)
```

### Benchmark (sin ventana, mide FPS)

```bash
./nbody_seq.exe --bench -n 5000 -f 100
./nbody_par.exe --bench -n 8000 -t 8 -f 100
```

### Script de Mediciones (genera CSV para Anexo 3)

```powershell
.\scripts\bench.ps1                           # 10 repeticiones, 50 frames cada una
.\scripts\bench.ps1 -Repeticiones 20 -Frames 100
```

Escribe `resultados/bench.csv` con tiempos y speedup.

## Opciones de Línea de Comandos

| Bandera | Descripción | Rango | Defecto |
|---------|-------------|-------|---------|
| `-n N` | Número de cuerpos | 2-200000 | 5000 |
| `-m MODO` | `colision` \| `galaxia` \| `nube` | - | colision |
| `-w W` | Ancho del canvas | 640-7680 | 1000 |
| `-h H` | Alto del canvas | 480-4320 | 700 |
| `-t T` | Hilos OpenMP (0=auto) | 0-1024 | 0 |
| `-s S` | Semilla RNG | 0-2³²-1 | 42 |
| `-G G` | Constante gravitacional | 0-1e6 | 100 |
| `-e E` | Softening (Plummer) | 0.001-10000 | 15 |
| `-d DT` | Paso de tiempo | 0.001-1.0 | 0.02 |
| `-f F` | Correr F frames (0=∞) | 0-1e8 | 0 |
| `--no-trails` | Sin estelas (borra cada frame) | - | off |
| `--bench` | Modo benchmark (sin ventana) | - | off |
| `--schedule S` | `static` \| `dynamic` \| `guided` | - | dynamic |
| `--chunk C` | Tamaño de trozo para reparto | 0-1e6 | 0 |
| `--help` | Muestra ayuda | - | - |

## Teclas Interactivas

- **1-9**: Cambiar número de hilos en vivo
- **ESPACIO**: Pausar/reanudar
- **R**: Reiniciar con otra semilla aleatoria
- **M**: Cambiar escenario (colisión → galaxia → nube)
- **T**: Alternar estelas on/off
- **ESC**: Salir

## Observar Colores Pseudoaleatorios

Los **colores ahora son pseudoaleatorios** (no solo por rapidez):
- Cada cuerpo tiene un "jitter de tono" único generado con su semilla
- El color final es: rapidez + jitter_tono (cíclico con fmod)
- Cuerpos con la misma rapidez pueden tener colores distintos 🌈
- Reiniciar con `-s SEED` diferente produce patrones de color nuevos

Comando para ver el efecto:
```bash
./nbody_par.exe -n 6000 -m galaxia -t 8
# Luego presiona R varias veces para reiniciar y ver nuevos colores
```

## Archivos Generados

- `nbody_seq.exe` / `nbody_seq` - Binario secuencial
- `nbody_par.exe` / `nbody_par` - Binario paralelo (OpenMP)
- `resultados/bench.csv` - Mediciones (ignorado por .gitignore)

## Notas de Performance

- **Secuencial**: Alcanza ~30 FPS con N ≈ 7000 en máquinas normales
- **Paralelo**: Lineal (8 hilos = 7x speedup típico)
- **Benchmark**: Mide sin ventana, más estable que modo interactivo
- **Reproducible**: Misma semilla = misma simulación (determinístico en lo posible)

## Troubleshooting

### `fatal error: SDL2/SDL.h: No such file or directory`
- Instala SDL2: `pacman -S mingw-w64-ucrt-x86_64-SDL2`
- Verifica la ruta: `-I/ucrt64/include/SDL2` en los flags

### `undefined reference to `SDL_CreateWindow'`
- Verifica: `-L/ucrt64/lib -lSDL2` en los flags de enlazado

### Performance muy lento (< 10 FPS)
- Reduce N: `./nbody_par -n 2000`
- Aumenta dt (paso): `./nbody_par -d 0.05`
- Verifica que uses la versión paralela: `./nbody_par -t 8`

