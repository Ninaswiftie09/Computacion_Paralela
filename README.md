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
- **Color**: rampa por rapidez (azul lento → cian → blanco → amarillo → naranja rápido)
  más un **desplazamiento de tono pseudoaleatorio por cuerpo**, sembrado con la misma
  semilla. Por eso dos cuerpos con la misma rapidez no salen del mismo color, y cambiar
  `-s` produce una paleta distinta.

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
| `-G G` / `-e E` / `-d DT` | Gravedad / softening (mín. 0.001) / paso | 100 / 15 / 0.02 |
| `-f F` | Correr F frames y salir | 0 |
| `--no-trails` | Sin estelas | off |
| `--bench` | Mide sin abrir ventana, emite una fila CSV | off |
| `--schedule S` | Reparto OpenMP: `static` \| `dynamic` \| `guided` | dynamic |
| `--chunk C` | Tamaño de trozo del reparto (0 = automático) | 0 |

**Teclas:** `1`-`9` hilos en vivo · `ESPACIO` pausa · `R` reinicia · `M` escenario ·
`T` estelas · `ESC` salir

**Por qué el mínimo de `-e` es 0.001 y no 0.** Por debajo de ~1e-19, `softening²`
guardado en `float` hace *underflow* a `0.0`, y el término `j == i` del bucle de
fuerzas (donde `dx = dy = 0` siempre) pasa de dar 0 a dar `1/√0 = inf` y luego
`0 · inf = NaN`, que contamina el sistema entero en un solo frame. El límite en
`cli.cpp` lo evita en la entrada; `nbody.cpp` tiene además un piso interno como
segunda capa, por si `SistemaNCuerpos` se usa fuera de la CLI.

## Medir

```powershell
.\scripts\bench.ps1                      # 10 repeticiones por combinación
.\scripts\bench.ps1 -Repeticiones 5 -Frames 30
```

Escribe `resultados/bench.csv` con `ms_fisica`, `ms_render`, `speedup` y
`eficiencia`. El speedup se mide contra el binario **secuencial**, no contra
`nbody_par -t 1`: `-fopenmp` por sí solo ya cambia el código generado, así que
ese sería un denominador inflado.

### Por qué el script ancla las corridas de un hilo a un núcleo

Las mediciones de un solo hilo se fijan al procesador lógico 0. Sin eso, Windows
detecta el proceso largo de un hilo y lo baja a un E-core:

| frames medidos | ms/paso (N = 16000, secuencial) |
|---|---|
| 2 | 162.7 |
| 5 | 160.6 |
| 15 | 434.7 |
| 40 | 683.5 |

Con corridas cortas el escalamiento es O(N²) limpio (10.2 → 43.1 → 184.5 ms al
duplicar N), pero en corridas largas la referencia se degrada 4× y el speedup
sale **superlineal**, con eficiencias sobre 100% que son físicamente imposibles.
La referencia serial tiene que medirse en un núcleo rápido; las corridas
multi-hilo se dejan libres para que usen toda la máquina.

**Limitación conocida.** Con 2 y 4 hilos y N grande los tiempos son erráticos, y
2 hilos puede salir más lento que 1. La causa es la misma heterogeneidad: el SO
reparte esos pocos hilos entre P-cores y E-cores sin criterio estable. Lo normal
sería fijarlos con `OMP_PLACES=cores` y `OMP_PROC_BIND=spread`, pero la libgomp
de MinGW responde `Affinity not supported on this configuration`, así que en
Windows no hay forma portable de anclarlos. A partir de 8 hilos el efecto se
diluye y la curva vuelve a ser limpia.

### Por qué `dynamic` y no `static`

La regla de libro dice que con carga uniforme gana `static`. Medido, no:

| hilos | static | dynamic | guided |
|---|---|---|---|
| 8 | 14.57 | **13.97** | 14.13 |
| 16 | 11.63 | **9.96** | 10.39 |
| 32 | 10.65 | **8.60** | 9.27 |

*(ms por paso, N = 8000)*

La regla supone que todos los núcleos son iguales. Este i9-13900HX es híbrido:
24 núcleos físicos entre P-cores rápidos y E-cores lentos. Aunque cada hilo
reciba la misma *cantidad* de trabajo, el que cae en un E-core tarda más y los
demás lo esperan en la barrera. `dynamic` reparte sobre la marcha, y por eso su
ventaja crece con el número de hilos (4% con 8, 19% con 32).

### Nota sobre `-fopenmp` y vectorización

Con 1 hilo el binario paralelo corre a ~la mitad del secuencial. No es un error
de medición: al mover el cuerpo de la región paralela a una función generada,
GCC deja de vectorizar el bucle interno. Se ve con `-fopt-info-vec`:

```
sin -fopenmp:  loop vectorized using 32 byte vectors and unroll factor 8
con -fopenmp:  not vectorized: unsupported control flow in loop
```

Se probaron `omp simd`, extraer el bucle a su propia función, devolverla por
valor y bloqueo de registros: las cuatro empeoraron el tiempo absoluto. Quedó la
forma más rápida de las medidas.

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
