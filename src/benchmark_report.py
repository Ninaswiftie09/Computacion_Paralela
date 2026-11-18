"""Tablas, speedup y gráficos a partir de los resultados de src/benchmark.py.

Uso:
  python -m src.benchmark_report --dir results/benchmark
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import matplotlib
import matplotlib.ticker

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.colors import LinearSegmentedColormap

from .utils import PROJECT_ROOT

NOMBRES = {"cpu": "CPU", "gpu_directo": "GPU directo", "gpu_optimizado": "GPU optimizado"}
COLORES = {"cpu": "#2a78d6", "gpu_directo": "#eb6834", "gpu_optimizado": "#1baf7a"}
COLORES_FASE = {
    "Preparación": "#eda100",
    "Guardado y carga": "#e87ba4",
    "Configuración": "#008300",
    "Entrenamiento": "#6250d6",
    "Prueba": "#e34948",
}
TINTA, TINTA_2, TENUE, REJILLA, EJE = "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
SUPERFICIE = "#fcfcfb"
AZULES = ["#fcfcfb", "#cde2fb", "#9ec5f4", "#6da7ec", "#3987e5", "#256abf", "#184f95", "#0d366b"]


def estilo() -> None:
    plt.rcParams.update({
        "font.family": ["Segoe UI", "DejaVu Sans"],
        "font.size": 10,
        "axes.titlesize": 12,
        "axes.titleweight": "bold",
        "axes.titlelocation": "left",
        "axes.labelcolor": TINTA_2,
        "axes.edgecolor": EJE,
        "axes.facecolor": SUPERFICIE,
        "figure.facecolor": SUPERFICIE,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "axes.grid": True,
        "grid.color": REJILLA,
        "grid.linewidth": 0.6,
        "xtick.color": TENUE,
        "ytick.color": TENUE,
        "xtick.labelcolor": TINTA_2,
        "ytick.labelcolor": TINTA_2,
        "legend.frameon": False,
        "savefig.dpi": 200,
        "savefig.bbox": "tight",
    })


def describir(serie: pd.Series) -> dict:
    media = float(serie.mean())
    desv = float(serie.std(ddof=1)) if len(serie) > 1 else 0.0
    return dict(media_s=media, desv_s=desv, cv_pct=100 * desv / media if media else 0.0,
                minimo_s=float(serie.min()), maximo_s=float(serie.max()), n=int(len(serie)))


def tabla_tiempos(prep: pd.DataFrame, trans: pd.DataFrame, entr: pd.DataFrame) -> pd.DataFrame:
    filas = []
    for columna, fase in (("seleccion_s", "Selección"), ("preparacion_s", "Preparación de imágenes"),
                          ("guardado_s", "Guardado NPZ"), ("carga_s", "Carga NPZ")):
        filas.append(dict(fase=fase, objetivo="cpu", **describir(prep[columna])))
    for memoria, grupo in trans.groupby("memoria"):
        filas.append(dict(fase=f"Transferencia RAM a VRAM ({memoria})", objetivo="gpu",
                          **describir(grupo["segundos"])))
    for objetivo, grupo in entr.groupby("objetivo", sort=False):
        for columna, fase in (("setup_s", "Configuración"), ("train_s", "Entrenamiento"),
                              ("epoch_mean_s", "Época promedio"), ("test_s", "Prueba"),
                              ("h2d_s", "Copia por lotes a GPU")):
            if columna == "h2d_s" and objetivo != "gpu_directo":
                continue
            filas.append(dict(fase=fase, objetivo=objetivo, **describir(grupo[columna])))
    return pd.DataFrame(filas)


def totales(prep: pd.DataFrame, entr: pd.DataFrame) -> pd.DataFrame:
    comun = float((prep["seleccion_s"] + prep["preparacion_s"]).mean())
    io = float((prep["guardado_s"] + prep["carga_s"]).mean())
    filas = []
    for objetivo, grupo in entr.groupby("objetivo", sort=False):
        filas.append({
            "objetivo": objetivo,
            "Preparación": comun,
            "Guardado y carga": io,
            "Configuración": grupo["setup_s"].mean(),
            "Entrenamiento": grupo["train_s"].mean(),
            "Prueba": grupo["test_s"].mean(),
        })
    tabla = pd.DataFrame(filas).set_index("objetivo")
    tabla["Total"] = tabla.sum(axis=1)
    return tabla


def tabla_speedup(entr: pd.DataFrame, total: pd.DataFrame) -> pd.DataFrame:
    medias = entr.groupby("objetivo")[["train_s", "epoch_mean_s", "test_s"]].mean()
    filas = []
    if "cpu" not in medias.index:
        return pd.DataFrame()
    comun = total.loc["cpu", ["Preparación", "Guardado y carga"]].sum()
    p = 1 - comun / total.loc["cpu", "Total"]
    for objetivo in medias.index:
        if objetivo == "cpu":
            continue
        s_entr = medias.loc["cpu", "train_s"] / medias.loc[objetivo, "train_s"]
        filas += [
            dict(fase="Entrenamiento", objetivo=objetivo, speedup=s_entr),
            dict(fase="Prueba", objetivo=objetivo,
                 speedup=medias.loc["cpu", "test_s"] / medias.loc[objetivo, "test_s"]),
            dict(fase="Pipeline completo", objetivo=objetivo,
                 speedup=total.loc["cpu", "Total"] / total.loc[objetivo, "Total"]),
            dict(fase="Pipeline según Amdahl", objetivo=objetivo,
                 speedup=1 / ((1 - p) + p / s_entr)),
        ]
    filas.append(dict(fase="Límite de Amdahl", objetivo="cualquiera", speedup=1 / (1 - p)))
    return pd.DataFrame(filas)


def consistencia(prep: pd.DataFrame, entr: pd.DataFrame) -> dict:
    resultado = {"huella_preparacion_identica": bool(prep["huella"].nunique() == 1)}
    acc_cpu = entr.loc[entr["objetivo"] == "cpu", "test_acc"].mean()
    for objetivo, grupo in entr.groupby("objetivo", sort=False):
        mediana = grupo["train_s"].median()
        atipicas = grupo.loc[grupo["train_s"] > 1.5 * mediana, "corrida"].tolist()
        resultado[objetivo] = dict(
            test_acc_media=float(grupo["test_acc"].mean()),
            test_acc_min=float(grupo["test_acc"].min()),
            test_acc_max=float(grupo["test_acc"].max()),
            val_acc_final_media=float(grupo["val_acc_final"].mean()),
            diferencia_test_acc_contra_cpu=float(grupo["test_acc"].mean() - acc_cpu),
            cv_entrenamiento_pct=float(100 * grupo["train_s"].std(ddof=1) / grupo["train_s"].mean())
            if len(grupo) > 1 else 0.0,
            corridas_atipicas=atipicas,
            memoria_pico_mb=float(grupo["peak_mem_mb"].max()),
        )
    return resultado


def etiqueta_barra(eje, barras, valores, formato, extra=None) -> None:
    extra = extra if extra is not None else [0] * len(valores)
    for barra, valor, mas in zip(barras, valores, extra):
        eje.annotate(formato.format(valor),
                     (barra.get_width() + mas, barra.get_y() + barra.get_height() / 2),
                     xytext=(4, 0), textcoords="offset points", va="center", color=TINTA, fontsize=9)


def figura_tiempos(entr: pd.DataFrame, epocas: int, destino: Path) -> None:
    objetivos = [o for o in NOMBRES if o in set(entr["objetivo"])]
    figura, ejes = plt.subplots(1, 2, figsize=(10, 3.2))
    for eje, (columna, titulo) in zip(ejes, (("train_s", f"Entrenamiento de {epocas} épocas"),
                                             ("test_s", "Evaluación del conjunto de prueba"))):
        medias = [entr.loc[entr["objetivo"] == o, columna].mean() for o in objetivos]
        desvs = np.nan_to_num([entr.loc[entr["objetivo"] == o, columna].std(ddof=1) for o in objetivos])
        posiciones = np.arange(len(objetivos))[::-1]
        barras = eje.barh(posiciones, medias, height=0.55, xerr=desvs,
                          color=[COLORES[o] for o in objetivos],
                          error_kw=dict(ecolor=TINTA_2, lw=1, capsize=3))
        eje.set_yticks(posiciones, [NOMBRES[o] for o in objetivos])
        eje.set_title(titulo)
        eje.set_xlabel("Segundos, media y desviación estándar")
        eje.grid(axis="y", visible=False)
        eje.set_xlim(0, max(m + d for m, d in zip(medias, desvs)) * 1.3)
        etiqueta_barra(eje, barras, medias, "{:.3g} s", extra=desvs)
    figura.tight_layout()
    figura.savefig(destino / "tiempos_por_fase.png")
    plt.close(figura)


def figura_speedup(speedup: pd.DataFrame, destino: Path) -> None:
    datos = speedup[speedup["objetivo"] != "cualquiera"]
    if datos.empty:
        return
    fases = ["Entrenamiento", "Prueba", "Pipeline completo"]
    objetivos = [o for o in ("gpu_directo", "gpu_optimizado") if o in set(datos["objetivo"])]
    figura, eje = plt.subplots(figsize=(8, 3.4))
    alto = 0.36
    base = np.arange(len(fases))[::-1]
    for i, objetivo in enumerate(objetivos):
        valores = [datos.loc[(datos["fase"] == f) & (datos["objetivo"] == objetivo), "speedup"].iloc[0]
                   for f in fases]
        barras = eje.barh(base + (0.5 - i) * (alto + 0.04), valores, height=alto,
                          color=COLORES[objetivo], label=NOMBRES[objetivo])
        etiqueta_barra(eje, barras, valores, "{:.1f}x")
    limite = speedup.loc[speedup["fase"] == "Límite de Amdahl", "speedup"]
    eje.axvline(1, color=TINTA_2, lw=1)
    eje.set_yticks(base, fases)
    eje.set_xlabel("Speedup (tiempo CPU entre tiempo GPU)")
    eje.set_title("Aceleración frente a la CPU")
    eje.grid(axis="y", visible=False)
    eje.set_xlim(0, eje.get_xlim()[1] * 1.12)
    if not limite.empty:
        eje.text(0.99, 0.02, f"Límite de Amdahl del pipeline: {limite.iloc[0]:.1f}x",
                 transform=eje.transAxes, ha="right", va="bottom", color=TINTA_2, fontsize=9)
    eje.legend(loc="upper right", ncols=2, bbox_to_anchor=(1, 1.16))
    figura.tight_layout()
    figura.savefig(destino / "speedup.png")
    plt.close(figura)


def figura_pipeline(total: pd.DataFrame, destino: Path) -> None:
    figura, eje = plt.subplots(figsize=(10, 2.9))
    objetivos = [o for o in NOMBRES if o in total.index]
    posiciones = np.arange(len(objetivos))[::-1]
    izquierda = np.zeros(len(objetivos))
    for fase, color in COLORES_FASE.items():
        valores = total.loc[objetivos, fase].to_numpy()
        eje.barh(posiciones, valores, left=izquierda, height=0.55, color=color, label=fase,
                 edgecolor=SUPERFICIE, linewidth=1.5)
        izquierda += valores
    for posicion, valor in zip(posiciones, izquierda):
        eje.annotate(f"{valor:.1f} s", (valor, posicion), xytext=(4, 0), textcoords="offset points",
                     va="center", color=TINTA, fontsize=9)
    eje.set_yticks(posiciones, [NOMBRES[o] for o in objetivos])
    eje.set_xlabel("Segundos, media de las corridas")
    eje.set_title("Composición del tiempo total del pipeline")
    eje.grid(axis="y", visible=False)
    eje.set_xlim(0, izquierda.max() * 1.12)
    eje.legend(loc="upper center", ncols=5, bbox_to_anchor=(0.5, -0.3))
    figura.tight_layout()
    figura.savefig(destino / "pipeline_composicion.png")
    plt.close(figura)


def figura_exactitud(epocas: pd.DataFrame, destino: Path) -> None:
    """Exactitud de validación por época, promedio de las corridas de cada objetivo."""
    figura, eje = plt.subplots(figsize=(8, 3.6))
    for objetivo in [o for o in NOMBRES if o in set(epocas["objetivo"])]:
        media = epocas[epocas["objetivo"] == objetivo].groupby("epoca")["val_acc"].mean() * 100
        eje.plot(media.index, media.values, color=COLORES[objetivo], lw=2, label=NOMBRES[objetivo],
                 marker="o", markersize=5, markeredgecolor=SUPERFICIE)
    eje.set_xticks(sorted(epocas["epoca"].unique()))
    eje.set_xlabel("Época")
    eje.set_ylabel("Exactitud de validación (%)")
    eje.set_title("Convergencia del modelo en cada dispositivo")
    eje.legend(loc="lower right")
    figura.tight_layout()
    figura.savefig(destino / "exactitud_por_epoca.png")
    plt.close(figura)


def figura_epocas(entr: pd.DataFrame, destino: Path) -> None:
    """Tiempo promedio por época en cada corrida, para ver la estabilidad de las mediciones."""
    figura, eje = plt.subplots(figsize=(8, 3.6))
    for objetivo in [o for o in NOMBRES if o in set(entr["objetivo"])]:
        grupo = entr[entr["objetivo"] == objetivo]
        eje.scatter(grupo["corrida"], grupo["epoch_mean_s"], s=36, color=COLORES[objetivo],
                    edgecolor=SUPERFICIE, linewidth=1.5, zorder=3)
        eje.plot(grupo["corrida"], grupo["epoch_mean_s"], color=COLORES[objetivo], lw=2,
                 label=NOMBRES[objetivo])
    eje.set_yscale("log")
    eje.set_yticks([1, 2, 5, 10, 20, 50], ["1", "2", "5", "10", "20", "50"])
    eje.yaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
    eje.set_xticks(sorted(entr["corrida"].unique()))
    eje.set_xlabel("Corrida")
    eje.set_ylabel("Segundos por época (escala log.)")
    eje.set_title("Tiempo promedio por época en cada corrida")
    eje.legend(loc="upper center", ncols=3, bbox_to_anchor=(0.5, -0.2))
    figura.tight_layout()
    figura.savefig(destino / "epocas_por_corrida.png")
    plt.close(figura)


def bloques_fase(fases: pd.DataFrame) -> list[tuple[str, float, float]]:
    """Une corridas consecutivas de la misma fase para rotular la línea de tiempo."""
    bloques = []
    for fila in fases.itertuples():
        nombre = "Preparación" if fila.fase == "preparacion" else (
            "Transferencia" if fila.fase == "transferencia" else NOMBRES.get(fila.objetivo, fila.objetivo))
        if bloques and bloques[-1][0] == nombre:
            bloques[-1] = (nombre, bloques[-1][1], fila.t_fin)
        else:
            bloques.append((nombre, fila.t_inicio, fila.t_fin))
    return bloques


def marcar_fases(eje, bloques, t0, rotular=True, sombra=True, ventana=None) -> None:
    inicio_v, fin_v = ventana if ventana else (t0, bloques[-1][2])
    duracion = fin_v - inicio_v
    for i, (nombre, inicio, fin) in enumerate(bloques):
        if sombra and i % 2 == 0:
            eje.axvspan((inicio - t0) / 60, (fin - t0) / 60, color=REJILLA, alpha=0.45, lw=0, zorder=0)
        visible = min(fin, fin_v) - max(inicio, inicio_v)
        if rotular and visible / duracion > 0.03:
            centro = (max(inicio, inicio_v) + min(fin, fin_v)) / 2
            eje.text((centro - t0) / 60, 1.02, nombre, transform=eje.get_xaxis_transform(),
                     ha="center", va="bottom", color=TINTA_2, fontsize=8)


def figura_monitor(destino: Path, origen: Path, fases: pd.DataFrame) -> None:
    cpu = pd.read_csv(origen / "monitor_cpu.csv")
    if cpu.empty or fases.empty:
        return
    bloques = bloques_fase(fases)
    t0 = bloques[0][1]
    minutos = (cpu["t"] - t0) / 60
    nucleos = [c for c in cpu.columns if c.startswith("nucleo_")]

    figura, eje = plt.subplots(figsize=(10, 4.2))
    mapa = LinearSegmentedColormap.from_list("azules", AZULES)
    imagen = eje.imshow(cpu[nucleos].T.to_numpy(), aspect="auto", cmap=mapa, vmin=0, vmax=100,
                        interpolation="nearest",
                        extent=[minutos.iloc[0], minutos.iloc[-1], len(nucleos) - 0.5, -0.5])
    eje.grid(False)
    eje.set_xlabel("Minutos desde el inicio del benchmark")
    eje.set_ylabel("Núcleo lógico")
    eje.set_yticks(range(0, len(nucleos), 4))
    for _, inicio, _ in bloques[1:]:
        eje.axvline((inicio - t0) / 60, color=TINTA, lw=0.8)
    marcar_fases(eje, bloques, t0, sombra=False)
    eje.set_title("Uso de cada núcleo del CPU", pad=18)
    barra = figura.colorbar(imagen, ax=eje, pad=0.01)
    barra.set_label("Uso (%)", color=TINTA_2)
    barra.outline.set_visible(False)
    figura.tight_layout()
    figura.savefig(destino / "uso_cpu_nucleos.png")
    plt.close(figura)

    gpu_path = origen / "monitor_gpu.csv"
    gpu = pd.read_csv(gpu_path) if gpu_path.exists() else pd.DataFrame()
    if gpu.empty:
        return
    dibujar_gpu(gpu, bloques, t0, destino / "uso_gpu.png", None)
    gpu_fases = fases[(fases["fase"] == "entrenamiento") & fases["objetivo"].str.startswith("gpu")]
    if not gpu_fases.empty:
        ventana = (gpu_fases["t_inicio"].min() - 20, gpu_fases["t_fin"].max() + 10)
        dibujar_gpu(gpu, bloques, t0, destino / "uso_gpu_detalle.png", ventana)


def dibujar_gpu(gpu: pd.DataFrame, bloques, t0: float, archivo: Path, ventana) -> None:
    if ventana:
        gpu = gpu[(gpu["t"] >= ventana[0]) & (gpu["t"] <= ventana[1])]
    minutos_gpu = (gpu["t"] - t0) / 60
    figura, ejes = plt.subplots(2, 1, figsize=(10, 4.6), sharex=True)
    series = (("utilization.gpu", "Uso de núcleos de la GPU (%)", (0, 105)),
              ("memory.used", "Memoria de la GPU en uso (MiB)", None))
    for i, (eje, (columna, titulo, limites)) in enumerate(zip(ejes, series)):
        marcar_fases(eje, bloques, t0, rotular=i == 0, ventana=ventana)
        eje.plot(minutos_gpu, gpu[columna], color=TINTA, lw=1.5)
        eje.set_title(titulo, fontsize=10, pad=16 if i == 0 else 6)
        if limites:
            eje.set_ylim(*limites)
        else:
            eje.set_ylim(0, gpu["memory.total"].iloc[0] * 1.05)
            eje.axhline(gpu["memory.total"].iloc[0], color=TENUE, lw=1, ls="--")
            eje.text(minutos_gpu.iloc[0], gpu["memory.total"].iloc[0], " capacidad total",
                     va="bottom", color=TINTA_2, fontsize=8)
        if ventana:
            eje.set_xlim((ventana[0] - t0) / 60, (ventana[1] - t0) / 60)
    ejes[-1].set_xlabel("Minutos desde el inicio del benchmark")
    figura.tight_layout()
    figura.savefig(archivo)
    plt.close(figura)


def main() -> None:
    parser = argparse.ArgumentParser(description="Tablas y gráficos del benchmark")
    parser.add_argument("--dir", default="results/benchmark")
    args = parser.parse_args()
    origen = Path(args.dir)
    origen = origen if origen.is_absolute() else PROJECT_ROOT / origen
    destino = origen / "figuras"
    destino.mkdir(parents=True, exist_ok=True)
    estilo()

    prep = pd.read_csv(origen / "corridas_preparacion.csv")
    entr = pd.read_csv(origen / "corridas_entrenamiento.csv")
    trans_path = origen / "transferencias.csv"
    trans = pd.read_csv(trans_path) if trans_path.stat().st_size > 2 else pd.DataFrame(columns=["memoria", "segundos"])
    epocas = pd.read_csv(origen / "epocas.csv")
    fases = pd.read_csv(origen / "fases.csv")

    tiempos = tabla_tiempos(prep, trans, entr)
    total = totales(prep, entr)
    speedup = tabla_speedup(entr, total)
    revision = consistencia(prep, entr)

    tiempos.to_csv(origen / "tabla_tiempos.csv", index=False)
    total.to_csv(origen / "tabla_pipeline.csv")
    speedup.to_csv(origen / "tabla_speedup.csv", index=False)
    if not trans.empty:
        trans.groupby("memoria")[["segundos", "gb_por_s"]].mean().to_csv(origen / "tabla_transferencia.csv")
    (origen / "consistencia.json").write_text(json.dumps(revision, indent=2, ensure_ascii=False),
                                              encoding="utf-8")

    figura_tiempos(entr, int(epocas["epoca"].max()), destino)
    figura_speedup(speedup, destino)
    figura_pipeline(total, destino)
    figura_epocas(entr, destino)
    figura_exactitud(epocas, destino)
    figura_monitor(destino, origen, fases)

    pd.set_option("display.width", 140)
    print(tiempos[["fase", "objetivo", "media_s", "desv_s", "cv_pct", "n"]].round(3).to_string(index=False))
    print()
    print(total.round(2).to_string())
    print()
    print(speedup.round(2).to_string(index=False))
    print()
    print(json.dumps(revision, indent=2, ensure_ascii=False))
    print(f"\nFiguras en {destino}")


if __name__ == "__main__":
    main()
