# Propuesta técnica — Screensaver "Detonación"

Proyecto #1, Computación Paralela y Distribuida (UVG, Sección 20).
Documento base: define el tema, el modelo, el plan de paralelización y el cronograma.
Es un documento vivo; se ajusta conforme avance la implementación.

---

## 1. Concepto

Screensaver que simula, en vista lateral 2D, el ciclo completo de una detonación nuclear
sobre un horizonte. El ciclo tiene cinco fases encadenadas por tiempo, y al terminar se
reinicia con otra semilla:

| # | Fase | Duración aprox. | Qué se ve |
|---|------|-----------------|-----------|
| 1 | **Destello** | 0.0 – 0.3 s | Pantalla lavada a blanco, caída exponencial de la luminancia |
| 2 | **Bola de fuego** | 0.3 – 2.0 s | Esfera incandescente que crece según la ley de expansión autosemejante |
| 3 | **Onda de choque** | 1.0 – 4.0 s | Anillo que se expande e imparte impulso radial a todo lo que toca |
| 4 | **Escombros** | 1.5 – 8.0 s | Partículas lanzadas, con arrastre, gravedad y rebote contra el suelo |
| 5 | **Hongo** | 4.0 – 15.0 s | Columna ascendente + toroide que se enrolla, enfriándose a humo gris |

Las fases se **traslapan**: mientras la onda de choque todavía viaja, los escombros ya están
en vuelo y la columna empieza a subir. Ese traslape es justamente lo que mantiene al
procesador ocupado con las N partículas todo el tiempo.

> Nota de alcance: esto es una simulación **visual** de un fenómeno físico bien documentado
> (expansión de una onda de choque esférica en un medio). No modela nada del diseño del
> artefacto; el modelo es el mismo que describe cualquier explosión puntual intensa.

---

## 2. Modelo físico y matemático

Esto cubre el requisito de *"incorporar algún elemento de física o trigonometría"*, y da
material citable y verificable para el informe.

### 2.1 Radio de la bola de fuego — solución de Taylor–von Neumann–Sedov

Para una liberación puntual de energía `E` en un medio de densidad `rho`, el radio del frente
de choque crece de forma autosemejante:

```
R(t) = C * (E / rho)^(1/5) * t^(2/5)
```

Con `C ~ 1.03` para aire (gamma = 1.4). El exponente es 2/5, no lineal: la bola de fuego
crece rapidísimo al inicio y luego se frena — que es exactamente lo que se ve en las películas
de archivo, y por eso se ve bien sin necesidad de trucos.

La sobrepresión en el frente decae como `dP ~ R^-3`, lo que se usa para atenuar el impulso
que la onda le entrega a las partículas conforme se aleja.

### 2.2 Impulso de la onda de choque sobre cada partícula

Cuando el frente `R(t)` cruza a una partícula en `p`, se le aplica un impulso radial:

```
d   = p - centro
u   = d / |d|                       (dirección unitaria)
J   = k * dP(R) * exp(-|d| / lambda)   (magnitud, atenuada con la distancia)
v  += J * u
T  += alphaT * J                    (la partícula se calienta con el choque)
```

### 2.3 Integración por partícula (Euler semi-implícito)

```
v += (g + arrastre(v) + flotacion(T) + vortice(p)) * dt
p += v * dt
T -= enfriamiento(T) * dt
```

- **Arrastre**: `-c_d * |v| * v` (cuadrático, domina en escombros rápidos).
- **Flotación**: `+beta * (T - T_ambiente) * y_hat` — el aire caliente sube. Es lo que forma
  el tallo del hongo.
- **Rebote**: si `p.y > suelo`, entonces `p.y = suelo` y `v.y = -e * v.y`, con `e ~ 0.4`.
  Levanta polvo en el impacto.

### 2.4 Vórtice toroidal (el sombrero del hongo) — trigonometría

El sombrero es un anillo de vórtice: el gas sube por el centro, se abre y se enrolla hacia
abajo por los bordes. Se modela con un campo de velocidad rotacional alrededor de un anillo
de radio `a` a la altura `h(t)`:

```
theta   = atan2(p.y - h, p.x - cx)
r_local = distancia de p al núcleo del anillo
omega   = Gamma / (2*pi * max(r_local, eps))
v      += omega * (-sin(theta), cos(theta))     <- la rotación que enrolla el sombrero
```

### 2.5 Color por temperatura (paleta de cuerpo negro)

En vez de colores al azar, cada partícula mapea su temperatura a un color por interpolación
sobre una rampa: `blanco -> amarillo -> naranja -> rojo -> gris humo`. Como cada partícula se
enfría a distinto ritmo (jitter pseudoaleatorio en su coeficiente de enfriamiento y en su
temperatura inicial), el resultado son **cientos de tonos distintos simultáneos** —
cumpliendo el requisito de colores pseudoaleatorios, pero con una razón física detrás.

---

## 3. Stack técnico

| Componente | Decisión | Razón |
|---|---|---|
| Lenguaje | C++17 | Permitido por el enunciado; structs planas para los datos, sin sobre-ingeniería |
| Gráficos | **SDL2** | Es la librería que sugiere el enunciado; acceso directo a un framebuffer, ideal para paralelizar el render |
| Paralelismo | **OpenMP** | Requisito explícito |
| Build | `Makefile` + `g++ -O2 -fopenmp` | Dos targets: `detonacion_seq` y `detonacion_par` |
| Toolchain (Windows) | MSYS2 / MinGW-w64 | `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2` |

**Layout de datos: SoA, no AoS.** Las partículas se guardan como arreglos paralelos
(`float *px, *py, *vx, *vy, *temp, ...`) en lugar de un arreglo de structs. Esto es
deliberado: mejora la localidad de caché, evita *false sharing* entre hilos y habilita
auto-vectorización. Es un punto fuerte para el informe.

---

## 4. Plan de paralelización (método PCAM)

### Partition (particionamiento)
Dos descomposiciones de dominio por frame:
- **Por partícula** — `N` tareas independientes de actualización física.
- **Por píxel / por banda de filas** — `W x H` tareas de acumulación de resplandor.

### Communication (comunicación)
Casi nula dentro de un frame: cada partícula solo lee estado global de solo-lectura
(centro de la explosión, `R(t)`, tiempo). Los únicos puntos de contacto son:
- La acumulación en el framebuffer (varias partículas escriben al mismo píxel).
- Las estadísticas agregadas (partículas vivas, temperatura máxima, FPS).

### Agglomeration (aglomeración)
- Partículas en bloques contiguos (`schedule(static)` si el costo es uniforme,
  `schedule(dynamic, chunk)` si hay divergencia por fases — se mide, no se asume).
- Framebuffer en **bandas de filas**, una por hilo, para que cada hilo escriba en una región
  disjunta y desaparezca la contención de escritura.

### Mapping (mapeo)
```c
#pragma omp parallel for schedule(guided)                          // física de partículas
#pragma omp parallel for collapse(2) schedule(static)              // campo de resplandor
#pragma omp parallel for reduction(+:vivas) reduction(max:t_max)   // estadísticas
```

### Mecanismos de sincronización y protección de memoria compartida
Esto vale 5% de la nota, así que va explícito:

1. **RNG por hilo.** `rand()` **no** es thread-safe y además serializa por su estado global.
   Se usa un xorshift32 con estado privado por hilo, sembrado con
   `semilla ^ omp_get_thread_num()`. Bonus: los resultados quedan **reproducibles** dada la
   misma semilla y el mismo número de hilos.
2. **Bandas disjuntas del framebuffer** en lugar de `critical` — se elimina la sección crítica
   por diseño, que siempre es mejor que protegerla.
3. **`reduction`** para los contadores agregados, no `atomic` dentro de un bucle caliente.
4. **Barrera implícita** al final de cada `parallel for`: la física de *todas* las partículas
   debe terminar antes de que empiece el render, o se dibujan estados mezclados.
5. **Padding / alineación** en los acumuladores por hilo para evitar *false sharing*.

---

## 5. Interfaz de línea de comandos

Cero variables hard-coded (requisito explícito). Todo parametrizado y validado:

```
detonacion_par [opciones]
  -n, --particles N     Número de partículas          (default 20000, rango 1..5000000)
  -w, --width W         Ancho del canvas               (default 800,  mínimo 640)
  -h, --height H        Alto del canvas                (default 600,  mínimo 480)
  -t, --threads T       Hilos OpenMP                   (default: omp_get_max_threads())
  -s, --seed S          Semilla del RNG                (default 42, para reproducibilidad)
  -e, --energy E        Energía en kilotones           (default 15.0, escala R(t))
  -f, --frames F        Correr F frames y salir        (0 = infinito; se usa en benchmarks)
      --no-glow         Desactiva el campo de resplandor por píxel
      --help            Ayuda
```

**Programación defensiva** — cada argumento se valida y el error se reporta de forma clara:
argumento no numérico, fuera de rango, faltante, flag desconocido, `N` que no cabe en memoria,
fallo al inicializar SDL, fallo de `malloc`. Nada de crashear con entrada mala.

---

## 6. Métricas y experimentos

El programa dibuja los **FPS** en pantalla (requisito) y además, en modo benchmark
(`--frames F`), imprime a CSV: `N, hilos, ms_fisica, ms_render, ms_total, fps_promedio`.

Plan de mediciones para el **Anexo 3** (mínimo 10 mediciones por prueba):
- Barrido de **N**: 1k, 10k, 50k, 100k, 500k, 1M partículas.
- Barrido de **hilos**: 1, 2, 4, 8, 16 (hasta los núcleos físicos disponibles).
- Por cada combinación: 10 corridas, se reporta mediana y desviación.
- Se calcula **speedup** `S(p) = T(1)/T(p)`, **eficiencia** `E(p) = S(p)/p`, y se contrasta
  contra **Amdahl** para estimar la fracción serial real (SDL presenta el frame en un solo
  hilo — esa es la parte irreduciblemente serial, y conviene decirlo en el informe).

Punto interesante para las conclusiones: como el screensaver está limitado por el vsync a
~60 FPS, el speedup **no** se ve como más FPS sino como **más partículas al mismo framerate**.
Vale la pena reportar la métrica "N máximo que sostiene 30 FPS" para secuencial vs. paralelo:
es más honesta y más vistosa en la presentación.

---

## 7. Cronograma

El enunciado marca entrega en la **semana del 26 al 30 de agosto** y todo el material subido
**antes de la clase del 6 de septiembre**. El historial de commits debe reflejar trabajo
sostenido desde al menos 2 semanas antes, así que se commitea seguido y en pedazos chicos.

| Bloque | Trabajo |
|---|---|
| Semana 1 (esta) | Tema y alcance definidos · esqueleto SDL + loop + contador de FPS · parser de CLI con validación |
| Semana 2 | **Versión secuencial completa**: partículas, fases, física, paleta, hongo · README |
| Semana 3 | **Versión paralela**: OpenMP en física y render · RNG por hilo · corrección de razas · benchmarks |
| Semana 4 | Optimización iterativa (schedules, SoA, `collapse`) · informe · anexos (diagrama de flujo, catálogo de funciones, bitácora) |

**Reparto sugerido (equipo de 3)** — con integración conjunta, no en silos:
- **A** — motor de partículas y modelo físico (fases 2–5, integración, colisiones).
- **B** — capa de render/SDL, paleta de cuerpo negro, campo de resplandor, HUD de FPS.
- **C** — CLI, programación defensiva, arnés de benchmark, gráficas de speedup, armado del informe.

---

## 8. Riesgos y mitigaciones

| Riesgo | Mitigación |
|---|---|
| El speedup no se nota (el render domina) | El campo de resplandor por píxel es la carga pesada y es paralela; se mide física y render por separado para aislar |
| Race conditions en el framebuffer | Bandas de filas disjuntas por hilo, no `critical` |
| `rand()` serializa y produce artefactos visibles | xorshift32 con estado por hilo desde el día uno |
| SDL2 no compila en Windows | Fallback: escribir frames PPM + visor, o usar el toolchain MSYS2 (documentado en el README) |
| Overhead de OpenMP con N chico | Se documenta el cruce: bajo cierto N el secuencial gana. Ese hallazgo **es** un resultado, no un fracaso |

---

## 9. Referencias iniciales

El enunciado pide al menos 3 citas relevantes y confiables. Estas son el punto de partida:

1. Taylor, G. I. (1950). *The Formation of a Blast Wave by a Very Intense Explosion. I.
   Theoretical Discussion.* Proceedings of the Royal Society A, 201(1065), 159–174.
2. Sedov, L. I. (1959). *Similarity and Dimensional Methods in Mechanics.* Academic Press.
3. OpenMP Architecture Review Board. *OpenMP Application Programming Interface, Version 5.2.*
   https://www.openmp.org/specifications/
4. Chapman, B., Jost, G., & van der Pas, R. (2007). *Using OpenMP: Portable Shared Memory
   Parallel Programming.* MIT Press.
5. SDL2 Documentation Wiki. https://wiki.libsdl.org/
