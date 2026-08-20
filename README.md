# Detonación — Screensaver paralelo con OpenMP

**Proyecto #1 — Computación Paralela y Distribuida (UVG, Sección 20)**

## Idea general

Un screensaver que simula la **detonación de una bomba atómica** vista de perfil:
el destello inicial, la bola de fuego que se expande, la onda de choque que barre el
terreno, los escombros que salen despedidos y rebotan, y finalmente la formación del
**hongo nuclear** por convección. Al terminar el ciclo, la escena se reinicia con una
semilla distinta, así que nunca se ve exactamente igual dos veces.

Visualmente es un sistema de **N partículas** (parámetro obligatorio del enunciado) donde
cada partícula lleva posición, velocidad y **temperatura**. El color no es aleatorio puro:
sale de una paleta de **cuerpo negro** (blanco → amarillo → naranja → rojo → humo gris)
indexada por la temperatura de cada partícula, con jitter pseudoaleatorio por partícula.
Eso da la variedad de colores que pide el enunciado y además se ve físicamente creíble:
las partículas se "enfrían" y cambian de color con el tiempo.

## Por qué este tema sirve para paralelizar

El trabajo por frame se divide en dos bucles grandes e **independientes entre elementos**:

1. **Actualización de N partículas** — cada partícula se integra por separado (empuje radial
   de la onda de choque, arrastre, gravedad, rebote contra el suelo, vórtice del hongo).
2. **Campo de calor / resplandor por píxel** — el brillo de cada píxel se acumula a partir de
   las fuentes cercanas. Es un bucle sobre el framebuffer, costoso y perfectamente divisible.

Los dos son casos de libro para `#pragma omp parallel for`, y el segundo garantiza que haya
suficiente carga computacional para que el *speedup* se note de verdad y no quede escondido
detrás del costo de dibujar.

## Stack

- **C++ + SDL2** para ventana, render y timing (la librería sugerida en el enunciado).
- **OpenMP** para la versión paralela.
- Compilación con `g++ -fopenmp` (MSYS2/MinGW en Windows).

## Estado

Avance de hoy: definición del tema y del alcance.
El detalle técnico (modelo físico, plan PCAM, parámetros CLI, plan de mediciones) está en
[docs/PROPUESTA.md](docs/PROPUESTA.md).
