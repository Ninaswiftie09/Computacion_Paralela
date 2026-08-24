# Screensaver de N cuerpos con gravedad

**Proyecto #1 — Computación Paralela y Distribuida (UVG, Sección 20)**

## Integrantes

| Nombre | Carné |
|---|---|
| Roberto Barreda | 23354 |
| Nina Najera | 231088 |
| Jose Anton | 221041 |

## Qué es

N cuerpos atrayéndose por gravitación newtoniana: galaxias que rotan, colisionan y forman
colas de marea. Todo el movimiento sale de una sola ley física.

```
a_i = G · Σ_j  m_j · (r_j − r_i) / (r_ij² + ε²)^(3/2)
v += a·dt   →   p += v·dt
```

- **ε** (softening de Plummer) evita que la fuerza explote si dos cuerpos casi se tocan;
  de paso hace que `j == i` aporte cero, así el bucle interno no necesita un `if`.
- **Trigonometría**: las estrellas nacen en órbita circular, `v = √(G·M_enc(r)/r)` aplicada
  tangencialmente → `v·(−sin θ, cos θ)`.
- **Color** por rapidez: azul (lento) → cian → blanco → amarillo → naranja (rápido).

## Por qué paralelizarlo

El costo es **O(N²)**. El bucle externo es independiente: cada `i` escribe solo `a[i]` y
únicamente **lee** las posiciones de los demás, así que no hay nada que proteger.

| N | FPS secuencial |
|---|---|
| 4 000 | 84.5 |
| 6 000 | 40.2 |
| 8 000 | 22.7 |
| 12 000 | 5.8 |

Desde N ≈ 7 000 la versión secuencial ya no alcanza los 30 FPS que pide el enunciado.

## Compilar

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-pkgconf
make          # ambas versiones
```

Los dos binarios salen de **los mismos fuentes**: los `#pragma omp` son inertes sin
`-fopenmp`. Misma física en ambos, así comparar tiempos es honesto.

## Usar

```bash
./nbody_par -n 8000 -m colision -t 8
./nbody_par --help
```

| Bandera | Descripción | Def. |
|---|---|---|
| `-n N` | Cuerpos (2..200000) | 5000 |
| `-m MODO` | `colision` \| `galaxia` \| `nube` | colision |
| `-t T` | Hilos OpenMP (0 = auto) | 0 |
| `-w W` / `-h H` | Canvas (mín. 640×480) | 1000×700 |
| `-s S` | Semilla | 42 |
| `-G G` / `-e E` / `-d DT` | Gravedad / softening / paso | 100 / 15 / 0.02 |
| `-f F` | Correr F frames y salir | 0 |
| `--no-trails` | Sin estelas | off |

**Teclas:** `1`-`9` hilos en vivo · `ESPACIO` pausa · `R` reinicia · `M` escenario ·
`T` estelas · `ESC` salir

## Archivos

| Archivo | Contenido |
|---|---|
| `src/nbody.{h,cpp}` | Escenarios, fuerzas O(N²), integración, energía |
| `src/render.{h,cpp}` | Framebuffer ARGB8888: estelas y destellos |
| `src/hud.{h,cpp}` | Fuente 5×7 propia y panel de métricas |
| `src/palette.{h,cpp}` | Color por rapidez |
| `src/cli.{h,cpp}` | Argumentos y validación |
| `src/rng.h` | Xorshift32, estado privado por cuerpo |
| `src/main.cpp` | Ventana SDL2, bucle, FPS |
