# Simulación de Ecosistema con OpenMP

Simulación de un ecosistema en una cuadrícula, con tres especies que se
alimentan, se reproducen, se mueven y mueren. El mismo conjunto de reglas se
ejecuta en dos modos: **secuencial** y **paralelo con OpenMP**, lo que permite
comparar rendimiento sobre exactamente la misma lógica.

| Símbolo | Habitante | Rol ecológico |
|:---:|---|---|
| `A` | Alga | Planta / productor |
| `G` | Gary (caracol) | Herbívoro / consumidor primario |
| `E` | Anguila | Carnívoro / depredador |
| `.` | Agua | Espacio disponible |

Con `--simbolos pdf` la cuadrícula usa la notación del enunciado: `P`, `H`, `C`.

## Requisitos

- Un compilador de C con soporte de OpenMP (`gcc` 9 o superior).
- `make`.

Verificado con **gcc 16.1.0 (MSYS2 UCRT64)** sobre Windows 11. En Linux o macOS
funciona igual; en macOS hay que usar `gcc` de Homebrew, porque el `clang` que
trae Apple no incluye OpenMP.

## Compilación

```bash
make
```

Eso produce dos binarios:

| Binario | Compilado con | Para qué sirve |
|---|---|---|
| `ecosistema_paralelo` | `-fopenmp` | Binario principal. Incluye ambos modos. |
| `ecosistema_secuencial` | sin OpenMP | Demuestra que el código no depende de la biblioteca. |

Compilar a mano:

```bash
gcc -O2 -Wall -Wextra -std=c11 -fopenmp ecosistema.c reglas.c salida.c cli.c benchmark.c main.c -o ecosistema_paralelo
```

## Ejecución

Corrida por defecto (cuadrícula 12x24, 10 ticks, secuencial):

```bash
./ecosistema_paralelo
```

Cuadrícula grande en paralelo con 8 hilos, sin imprimir el mapa:

```bash
./ecosistema_paralelo --filas 1024 --columnas 1024 --ticks 50 --modo paralelo --hilos 8 --sin-cuadricula
```

Generar el archivo de resultados con la notación del enunciado:

```bash
./ecosistema_paralelo --ticks 20 --cada 5 --simbolos pdf --salida resultados/resultados.txt
```

Ver todas las opciones:

```bash
./ecosistema_paralelo --ayuda
```

## Reproducir las mediciones

```bash
make bench
```

## Verificar que no hay race conditions

```bash
make verificar
```