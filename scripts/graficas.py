"""
graficas.py
Lee la bitácora en CSV, calcula promedios y speedups, y genera las figuras
del informe.

Uso: python scripts/graficas.py [resultados/bitacora.csv] [docs/informe/img]

El speedup de cada caso paralelo se mide contra el tiempo secuencial de la
misma dificultad. El tiempo secuencial es la base porque recorre el espacio
en un solo núcleo.
"""
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

RUTA_CSV = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("resultados/bitacora.csv")
DIR_IMG = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("docs/informe/img")
DIR_IMG.mkdir(parents=True, exist_ok=True)

ORDEN_DIF = ["facil", "media", "dificil", "naive_mejor", "naive_peor"]
ETIQUETA_DIF = {
    "facil": "Fácil",
    "media": "Media",
    "dificil": "Difícil",
    "naive_mejor": "Naive mejor",
    "naive_peor": "Naive peor",
}
COLOR = {
    "secuencial": "#555555",
    "naive": "#1f77b4",
    "ciclico": "#2ca02c",
    "permutado": "#d62728",
    "hibrido": "#9467bd",
}


def cargar(ruta):
    datos = pd.read_csv(ruta)
    for columna in ("iter_ganador", "segundos"):
        datos[columna] = pd.to_numeric(datos[columna], errors="coerce")
    return datos.dropna(subset=["segundos"])


def etiqueta_config(fila):
    """Nombre de una configuración paralela para las tablas y leyendas."""
    if fila["programa"] == "hibrido":
        return f"hibrido {fila['procesos']}x{fila['hilos']}"
    if fila["programa"] == "secuencial":
        return "secuencial"
    return f"{fila['programa']} np{fila['procesos']}"


def resumen(datos):
    """Promedia las repeticiones y agrega el speedup contra el secuencial."""
    claves = ["programa", "dificultad", "bits", "procesos", "hilos"]
    agregado = (
        datos.groupby(claves, as_index=False)
        .agg(
            segundos_media=("segundos", "mean"),
            segundos_desv=("segundos", "std"),
            iter_media=("iter_ganador", "mean"),
            corridas=("segundos", "size"),
        )
        .fillna({"segundos_desv": 0.0})
    )
    base = (
        agregado[agregado["programa"] == "secuencial"]
        .set_index("dificultad")["segundos_media"]
        .to_dict()
    )
    agregado["speedup"] = agregado.apply(
        lambda f: base.get(f["dificultad"], np.nan) / f["segundos_media"], axis=1
    )
    agregado["config"] = agregado.apply(etiqueta_config, axis=1)
    return agregado


def grafica_speedup_procesos(res):
    """Speedup contra número de procesos, un panel por dificultad. Se dejan el
    cíclico y el permutado. El naive da speedups de miles en unas llaves y
    menos de 1 en otras, así que su comportamiento va en la figura aparte."""
    difs = [d for d in ORDEN_DIF if d in res["dificultad"].unique()]
    procs = sorted(res["procesos"].unique())
    fig, ejes = plt.subplots(1, len(difs), figsize=(3.4 * len(difs), 4), sharey=True)
    if len(difs) == 1:
        ejes = [ejes]
    for eje, dif in zip(ejes, difs):
        for programa in ("ciclico", "permutado"):
            sub = res[(res["dificultad"] == dif) & (res["programa"] == programa)]
            sub = sub.sort_values("procesos")
            if not sub.empty:
                eje.plot(sub["procesos"], sub["speedup"], marker="o",
                         color=COLOR[programa], label=programa)
        eje.plot(procs, procs, color="#bbbbbb", linestyle="--", label="ideal")
        eje.set_title(ETIQUETA_DIF[dif])
        eje.set_xlabel("Procesos")
        eje.set_xticks(procs)
        eje.grid(True, alpha=0.3)
    ejes[0].set_ylabel("Speedup contra secuencial")
    ejes[-1].legend()
    fig.suptitle("Speedup del cíclico y el permutado por número de procesos")
    fig.tight_layout()
    fig.savefig(DIR_IMG / "speedup_procesos.png", dpi=130)
    plt.close(fig)


def grafica_consistencia(res):
    """Speedup de cada acercamiento con 4 procesos, por dificultad.
    Muestra que el naive es irregular y el cíclico y el permutado son estables."""
    difs = [d for d in ORDEN_DIF if d in res["dificultad"].unique()]
    programas = ("naive", "ciclico", "permutado")
    x = np.arange(len(difs))
    ancho = 0.25
    fig, eje = plt.subplots(figsize=(9, 4.5))
    for i, programa in enumerate(programas):
        alturas = []
        for dif in difs:
            sub = res[(res["dificultad"] == dif) & (res["programa"] == programa)
                      & (res["procesos"] == 4)]
            alturas.append(sub["speedup"].iloc[0] if not sub.empty else 0)
        eje.bar(x + (i - 1) * ancho, alturas, ancho, color=COLOR[programa], label=programa)
    eje.axhline(4, color="#bbbbbb", linestyle="--", label="speedup = 4")
    eje.set_yscale("log")
    eje.set_xticks(x)
    eje.set_xticklabels([ETIQUETA_DIF[d] for d in difs])
    eje.set_ylabel("Speedup contra secuencial (escala log)")
    eje.set_title("Speedup con 4 procesos según la dificultad de la llave")
    eje.legend()
    eje.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(DIR_IMG / "speedup_consistencia.png", dpi=130)
    plt.close(fig)


def grafica_iteraciones(res):
    """Iteraciones del proceso ganador con 4 procesos. El naive depende de la
    posición de la llave, el permutado no."""
    difs = [d for d in ORDEN_DIF if d in res["dificultad"].unique()]
    programas = ("naive", "ciclico", "permutado")
    x = np.arange(len(difs))
    ancho = 0.25
    fig, eje = plt.subplots(figsize=(9, 4.5))
    for i, programa in enumerate(programas):
        alturas = []
        for dif in difs:
            sub = res[(res["dificultad"] == dif) & (res["programa"] == programa)
                      & (res["procesos"] == 4)]
            alturas.append(sub["iter_media"].iloc[0] if not sub.empty else 0)
        eje.bar(x + (i - 1) * ancho, alturas, ancho, color=COLOR[programa], label=programa)
    eje.set_yscale("log")
    eje.set_xticks(x)
    eje.set_xticklabels([ETIQUETA_DIF[d] for d in difs])
    eje.set_ylabel("Iteraciones del proceso ganador (escala log)")
    eje.set_title("Trabajo del proceso ganador con 4 procesos")
    eje.legend()
    eje.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(DIR_IMG / "iteraciones_ganador.png", dpi=130)
    plt.close(fig)


def grafica_hibrido(res):
    """Speedup del híbrido comparado con el MPI puro del cíclico al mismo
    número de trabajadores, para la dificultad media. El cíclico es la base
    porque da un speedup estable para cualquier llave."""
    dif = "media" if "media" in res["dificultad"].unique() else res["dificultad"].iloc[0]
    sub = res[res["dificultad"] == dif]
    hib = sub[sub["programa"] == "hibrido"].sort_values(["procesos", "hilos"])
    base_mpi = sub[sub["programa"] == "ciclico"].set_index("procesos")["speedup"].to_dict()

    fig, eje = plt.subplots(figsize=(8, 4.5))
    etiquetas = [f"{p}x{h}" for p, h in zip(hib["procesos"], hib["hilos"])]
    x = np.arange(len(etiquetas))
    eje.bar(x, hib["speedup"], 0.55, color=COLOR["hibrido"], label="híbrido (procesos x hilos)")

    trabajadores = hib["procesos"].to_numpy() * hib["hilos"].to_numpy()
    referencia = [base_mpi.get(int(w), np.nan) for w in trabajadores]
    eje.plot(x, referencia, "D", color=COLOR["ciclico"], markersize=9,
             label="MPI puro con los mismos trabajadores")
    for xi, w in zip(x, trabajadores):
        eje.annotate(f"{int(w)} tot.", (xi, 0), textcoords="offset points",
                     xytext=(0, 4), ha="center", color="white", fontsize=9)

    eje.set_xticks(x)
    eje.set_xticklabels(etiquetas)
    eje.set_ylabel("Speedup contra secuencial")
    eje.set_title(f"Speedup del híbrido MPI y OpenMP, llave {ETIQUETA_DIF.get(dif, dif).lower()}")
    eje.legend()
    eje.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(DIR_IMG / "speedup_hibrido.png", dpi=130)
    plt.close(fig)


def main():
    if not RUTA_CSV.exists():
        sys.exit(f"No existe {RUTA_CSV}. Corre primero scripts/bitacora.sh")
    datos = cargar(RUTA_CSV)
    res = resumen(datos)

    ruta_resumen = RUTA_CSV.with_name("resumen.csv")
    columnas = ["programa", "dificultad", "procesos", "hilos", "corridas",
                "segundos_media", "segundos_desv", "iter_media", "speedup"]
    res.sort_values(["dificultad", "programa", "procesos", "hilos"])[columnas].to_csv(
        ruta_resumen, index=False, float_format="%.4f")

    grafica_speedup_procesos(res)
    grafica_consistencia(res)
    grafica_iteraciones(res)
    grafica_hibrido(res)
    print(f"Resumen: {ruta_resumen}")
    print(f"Figuras en {DIR_IMG}: speedup_procesos, speedup_consistencia, "
          "iteraciones_ganador, speedup_hibrido")


if __name__ == "__main__":
    main()
