# Screensaver de N cuerpos con gravedad

**Proyecto #1 — Computación Paralela y Distribuida (UVG, Sección 20)**

Un screensaver que simula **N cuerpos atrayéndose por gravitación newtoniana**. Se ven
galaxias que rotan, colisionan y forman colas de marea, o nubes de materia que colapsan
en cúmulos. Todo el movimiento sale de una sola ley física, sin efectos inventados.

## La matemática, completa

```
Ley de gravitación:  F = G · m_i · m_j / r²

Aceleración:         a_i = G · Σ_j  m_j · (r_j − r_i) / (r_ij² + ε²)^(3/2)

Integración:         v += a·dt   →   p += v·dt        (Euler semi-implícito)
```

- **ε (softening de Plummer)** evita que la fuerza explote cuando dos cuerpos casi se
  tocan. Como efecto secundario, el término `j == i` da exactamente cero, así que el
  bucle interno no necesita un `if (i != j)` y se puede vectorizar.
- **Trigonometría**: las estrellas nacen en órbita circular, con la velocidad
  `v = √(G · M_encerrada(r) / r)` aplicada **tangencialmente** → `v·(−sin θ, cos θ)`.
- **Color**: cada cuerpo se pinta según su **rapidez** — azul (lento) → cian → blanco →
  amarillo → naranja (rápido). Es la misma idea que el color de las estrellas por
  temperatura, y deja leer la dinámica de un vistazo.

## Por qué este problema se paraleliza tan bien

El costo es **O(N²)**: cada cuerpo interactúa con todos los demás, así que un frame con
N = 8000 hace **64 millones** de interacciones. El bucle externo (por cuerpo `i`) es
totalmente independiente — cada iteración escribe solo `a[i]` y únicamente **lee** las
posiciones de los demás — de modo que paralelizarlo no requiere proteger nada.

Medido en esta máquina (versión secuencial, `-O3 -march=native`):

| N      | FPS secuencial |
|--------|----------------|
| 4 000  | 84.5 |
| 6 000  | 40.2 |
| 8 000  | 22.7 |
| 12 000 |  5.8 |

Se ve el O(N²) puro: subir N de 4 000 a 6 000 (factor 1.5) cuesta 2.1× más tiempo,
contra el 2.25× teórico. **A partir de N ≈ 7 000 la versión secuencial ya no alcanza los
30 FPS que exige el enunciado** — ahí es donde la versión paralela deja de ser un lujo.

## Compilar

Requiere **g++ con OpenMP** y **SDL2**. En Windows con MSYS2 (UCRT64):

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-pkgconf
make            # compila las dos versiones
make seq        # solo nbody_seq  (sin OpenMP)
make par        # solo nbody_par  (con OpenMP)
```

Ambos binarios salen de **los mismos archivos fuente**: los `#pragma omp` son inertes si
no se compila con `-fopenmp`. Así la versión secuencial y la paralela ejecutan exactamente
la misma física, y comparar sus tiempos es honesto.

## Usar

```bash
./nbody_par -n 8000 -m colision -t 8     # dos galaxias chocando, 8 hilos
./nbody_par -n 2000 -m galaxia           # un solo disco en rotación
./nbody_par -n 5000 -m nube --no-trails  # colapso gravitacional, sin estelas
./nbody_par --help                       # todas las opciones
```

### Opciones

| Bandera | Descripción | Default |
|---|---|---|
| `-n, --particles N` | Número de cuerpos (2..200000) | 5000 |
| `-m, --mode MODO` | `colision` \| `galaxia` \| `nube` | colision |
| `-s, --seed S` | Semilla del generador | 42 |
| `-G, --gravity G` | Constante gravitacional | 100 |
| `-e, --softening E` | Suavizado ε en px (evita r = 0) | 15 |
| `-d, --dt DT` | Paso de tiempo (segundos) | 0.020 |
| `-w, --width W` | Ancho del canvas (mínimo 640) | 1000 |
| `-h, --height H` | Alto del canvas (mínimo 480) | 700 |
| `-t, --threads T` | Hilos OpenMP (0 = automático) | 0 |
| `-f, --frames F` | Correr F frames y salir (0 = infinito) | 0 |
| `--no-trails` | Borra la pantalla en vez de dejar estelas | off |
| `--help` | Muestra la ayuda | |

### Teclas

| Tecla | Acción |
|---|---|
| `1`–`9` | Cambia el número de hilos **en vivo** |
| `ESPACIO` | Pausa |
| `R` | Reinicia con otra semilla |
| `M` | Cambia de escenario |
| `T` | Alterna las estelas |
| `ESC` | Salir |

## Estructura

| Archivo | Contenido |
|---|---|
| `src/nbody.{h,cpp}` | Física: siembra de escenarios, fuerzas O(N²), integración, energía |
| `src/render.{h,cpp}` | Framebuffer ARGB8888: estelas y destellos aditivos |
| `src/palette.{h,cpp}` | Rampa de color por rapidez |
| `src/cli.{h,cpp}` | Parseo y validación de argumentos |
| `src/rng.h` | Xorshift32 con estado privado por cuerpo (sin estado global) |
| `src/main.cpp` | Ventana SDL2, bucle principal, medición de FPS |
