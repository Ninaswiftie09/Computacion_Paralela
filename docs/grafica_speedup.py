import csv
import os
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(REPO, "resultados", "benchmark.csv")
SALIDA = os.path.join(REPO, "docs", "speedup.png")
COLORES = ["#86b6ef", "#2a78d6", "#104281"]

SUPERFICIE = "#fcfcfb"
TINTA = "#0b0b0b"
TINTA_2 = "#52514e"
MUTED = "#898781"
GRID = "#e1e0d9"
EJE = "#c3c2b7"

# ---------------------------------------------------------------- datos
series = defaultdict(dict)
with open(CSV, newline="", encoding="utf-8") as f:
    for fila in csv.DictReader(f):
        if fila["modo"] != "paralelo":
            continue
        etiqueta = "{}x{}".format(fila["filas"], fila["columnas"])
        series[etiqueta][int(fila["hilos"])] = float(fila["speedup"])

orden = sorted(series, key=lambda e: int(e.split("x")[0]))
hilos = sorted({h for d in series.values() for h in d})

# ---------------------------------------------------------------- figura
fig, ax = plt.subplots(figsize=(6.4, 3.9), dpi=200)
fig.patch.set_facecolor(SUPERFICIE)
ax.set_facecolor(SUPERFICIE)

tope_y = 5.0

# Referencia ideal (y = x). Discontinua y gris: es una referencia, no un dato.
ax.plot([0, tope_y], [0, tope_y], linestyle=(0, (5, 4)), linewidth=1.4,
        color=MUTED, zorder=1)
ax.annotate("Speedup ideal", xy=(5.2, 4.62), color=MUTED, fontsize=8.5,
            ha="left", va="center")

for i, etiqueta in enumerate(orden):
    xs = [h for h in hilos if h in series[etiqueta]]
    ys = [series[etiqueta][h] for h in xs]
    ax.plot(xs, ys, marker="o", markersize=6.5, linewidth=2.0,
            color=COLORES[i], label=etiqueta + " celdas",
            markeredgecolor=SUPERFICIE, markeredgewidth=2.0,
            zorder=3 + i, clip_on=False)

# Linea de speedup = 1: por debajo, paralelizar sale mas caro que no hacerlo.
ax.axhline(1.0, color=EJE, linewidth=1.0, zorder=2)
ax.annotate("sin ganancia", xy=(16.4, 1.0), color=TINTA_2, fontsize=8,
            va="center", ha="left")

ax.set_xlim(0, 17.4)
ax.set_ylim(0, tope_y)
ax.set_xticks(hilos)
ax.set_xticklabels([str(h) for h in hilos])
ax.yaxis.set_major_locator(MultipleLocator(1))

ax.set_xlabel("Hilos", fontsize=10, color=TINTA_2, labelpad=7)
ax.set_ylabel("Speedup respecto al secuencial", fontsize=10, color=TINTA_2,
              labelpad=7)

ax.grid(axis="y", color=GRID, linewidth=0.8, zorder=0)
ax.set_axisbelow(True)

for lado in ("top", "right"):
    ax.spines[lado].set_visible(False)
for lado in ("left", "bottom"):
    ax.spines[lado].set_color(EJE)
    ax.spines[lado].set_linewidth(1.0)

ax.tick_params(colors=MUTED, labelsize=9, length=0)
for t in ax.get_xticklabels() + ax.get_yticklabels():
    t.set_color(TINTA_2)

# La leyenda va sobre el area de trazado: dentro chocaria con la linea
# ideal, que cruza justamente la esquina superior izquierda.
ax.legend(loc="lower center", bbox_to_anchor=(0.5, 1.0), ncol=3,
          frameon=False, fontsize=9, labelcolor=TINTA, handlelength=2.4,
          columnspacing=1.8, borderpad=0.0, handletextpad=0.6)

fig.tight_layout(pad=0.6)
os.makedirs(os.path.dirname(SALIDA), exist_ok=True)
fig.savefig(SALIDA, facecolor=SUPERFICIE, bbox_inches="tight")
print("escrito:", SALIDA)

for etiqueta in orden:
    print(" ", etiqueta, {h: round(series[etiqueta][h], 2) for h in hilos
                          if h in series[etiqueta]})
