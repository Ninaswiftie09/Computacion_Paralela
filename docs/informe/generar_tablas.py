"""Genera las tablas del informe a partir de los resultados medidos.

Uso, desde la raíz del repositorio:
  python docs/informe/generar_tablas.py
"""
from __future__ import annotations

import json
import re
from pathlib import Path

import pandas as pd

RAIZ = Path(__file__).resolve().parents[2]
BENCH = RAIZ / "results" / "benchmark"
KERAS = RAIZ / "results" / "keras_referencia"
PERFIL = RAIZ / "results" / "perfil_gpu"
SALIDA = Path(__file__).resolve().parent / "tablas"
GPU = ("gpu_directo", "gpu_optimizado", "gpu_numba")


def num(valor: float, decimales: int = 2) -> str:
    return f"{valor:,.{decimales}f}"


def pm(serie: pd.Series, decimales: int = 2) -> str:
    desv = serie.std(ddof=1) if len(serie) > 1 else 0.0
    return f"{num(serie.mean(), decimales)} $\\pm$ {num(desv, decimales)}"


def pct(valor: float, decimales: int = 2) -> str:
    return f"{100 * valor:.{decimales}f} \\%"


def latex(texto: str) -> str:
    return texto.replace("_", "\\_").replace("%", "\\%").replace("#", "\\#")


def escribir(nombre: str, filas: list[str]) -> None:
    (SALIDA / f"{nombre}.tex").write_text("\n".join(filas) + "\n", encoding="utf-8")


def cargar() -> dict:
    datos = {
        "prep": pd.read_csv(BENCH / "corridas_preparacion.csv"),
        "alm": pd.read_csv(BENCH / "corridas_almacenamiento.csv"),
        "trans": pd.read_csv(BENCH / "transferencias.csv"),
        "entr": pd.read_csv(BENCH / "corridas_entrenamiento.csv"),
        "epocas": pd.read_csv(BENCH / "epocas.csv"),
        "total": pd.read_csv(BENCH / "tabla_pipeline.csv").set_index("objetivo"),
        "speedup": pd.read_csv(BENCH / "tabla_speedup.csv"),
        "ajuste": pd.read_csv(KERAS / "ajuste_cnn_improved.csv"),
        "keras": pd.read_csv(KERAS / "validacion_keras_cpu.csv"),
        "keras_epocas": pd.read_csv(KERAS / "validacion_keras_epocas.csv"),
        "categorias": pd.read_csv(PERFIL / "categorias_estandar.csv"),
        "kernels": pd.read_csv(PERFIL / "kernels_estandar.csv"),
        "kernels_numba": pd.read_csv(PERFIL / "kernels_numba.csv"),
        "verificacion": json.loads((PERFIL / "verificacion_numba.json").read_text(encoding="utf-8")),
    }
    return datos


def tabla_secuencial(d: dict) -> None:
    filas = []
    for fila in d["ajuste"].itertuples():
        filas.append(f"Keras, {latex(fila.model_name)}, ajuste del Lab 3 & JPEG con tf.data & 3 & 1 & "
                     f"{num(fila.training_time_seconds)} & {pct(fila.validation_accuracy)} (val.) \\\\")
    k = d["keras"]
    filas.append(f"Keras, cnn\\_improved\\_a & NPZ preparado & 10 & {len(k)} & {pm(k['train_s'])} & "
                 f"{pct(k['test_acc'].mean())} \\\\")
    cpu = d["entr"][d["entr"]["objetivo"] == "cpu"]
    filas.append(f"PyTorch en CPU, cnn\\_improved\\_a & NPZ preparado & 10 & {len(cpu)} & {pm(cpu['train_s'])} & "
                 f"{pct(cpu['test_acc'].mean())} \\\\")
    escribir("secuencial", filas)


def tabla_preparacion(d: dict) -> None:
    prep = d["prep"]
    grupos = prep.groupby("workers")
    trabajo = grupos["preparacion_s"].mean()
    arranque = grupos["arranque_pool_s"].first()
    completo = trabajo + arranque
    filas = []
    for workers, grupo in grupos:
        filas.append(
            f"{workers} & {num(arranque.loc[workers]) if workers > 1 else '0.00'} & {pm(grupo['preparacion_s'])} & "
            f"{num(grupo['ms_por_imagen'].mean(), 3)} & {num(trabajo.loc[1] / trabajo.loc[workers])} & "
            f"{num(completo.loc[1] / completo.loc[workers])} \\\\")
    escribir("preparacion", filas)


def tabla_comparativa(d: dict) -> None:
    entr, prep, alm, trans = d["entr"], d["prep"], d["alm"], d["trans"]
    uno = prep[prep["workers"] == 1]
    comun = lambda serie: " & ".join([pm(serie)] * 4)
    vacio = "\\multicolumn{1}{c}{---}"
    por = {o: entr[entr["objetivo"] == o] for o in ("cpu", *GPU)}
    paginable = trans[trans["memoria"] == "paginable"]["segundos"]
    filas = [
        f"Selección del split & {comun(uno['seleccion_s'])} \\\\",
        f"Preparación de imágenes, 1 proceso & {comun(uno['preparacion_s'])} \\\\",
        f"Guardado del NPZ & {comun(alm['guardado_s'])} \\\\",
        f"Carga del NPZ & {comun(alm['carga_s'])} \\\\",
        f"Copia completa RAM a VRAM & {vacio} & {vacio} & {pm(paginable, 3)} & {pm(paginable, 3)} \\\\",
        f"Copia por lotes dentro del entrenamiento & {vacio} & {pm(por['gpu_directo']['h2d_s'], 3)} & {vacio} & {vacio} \\\\",
        "Configuración del modelo & " + " & ".join(pm(por[o]["setup_s"], 3) for o in por) + " \\\\",
        "Entrenamiento, 10 épocas & " + " & ".join(pm(por[o]["train_s"]) for o in por) + " \\\\",
        "Entrenamiento, mediana & " + " & ".join(num(por[o]["train_s"].median()) for o in por) + " \\\\",
        "Época promedio & " + " & ".join(pm(por[o]["epoch_mean_s"], 3) for o in por) + " \\\\",
        "Prueba & " + " & ".join(pm(por[o]["test_s"], 3) for o in por) + " \\\\",
        "\\midrule",
        "Total del pipeline & " + " & ".join(num(d["total"].loc[o, "Total"]) for o in por) + " \\\\",
    ]
    escribir("comparativa", filas)


def tabla_speedup(d: dict) -> None:
    s = d["speedup"]
    valor = lambda fase, objetivo: s.loc[(s["fase"] == fase) & (s["objetivo"] == objetivo), "speedup"].iloc[0]
    filas = []
    for fase in ("Entrenamiento", "Prueba", "Pipeline completo"):
        filas.append(f"{fase} & " + " & ".join(f"{num(valor(fase, o))}x" for o in GPU) + " \\\\")
    cpu = d["entr"][d["entr"]["objetivo"] == "cpu"]["train_s"].median()
    filas.append("Entrenamiento con medianas & " + " & ".join(
        f"{num(cpu / d['entr'][d['entr']['objetivo'] == o]['train_s'].median())}x" for o in GPU) + " \\\\")
    escribir("speedup", filas)

    prep = d["prep"].groupby("workers")
    mejor = int((prep["preparacion_s"].mean() + prep["arranque_pool_s"].first()).idxmin())
    filas = [
        f"Preparación de imágenes con {mejor} procesos, solo trabajo & "
        f"{num(valor('Preparación de imágenes', f'cpu_{mejor}'))}x \\\\",
        f"Preparación de imágenes con {mejor} procesos, con arranque del pool & "
        f"{num(valor('Preparación con arranque del pool', f'cpu_{mejor}'))}x \\\\",
        f"Pipeline con preparación paralela y GPU optimizado & "
        f"{num(valor('Pipeline completo', 'gpu_optimizado_paralelo'))}x \\\\",
        f"Límite de Amdahl con preparación secuencial & {num(valor('Límite de Amdahl', 'cualquiera'))}x \\\\",
    ]
    escribir("speedup_extra", filas)


def tabla_exactitud(d: dict) -> None:
    nombres = {"cpu": "PyTorch en CPU", "gpu_directo": "GPU directo", "gpu_optimizado": "GPU optimizado",
               "gpu_numba": "GPU con kernel Numba"}
    filas = []
    k, ke = d["keras"], d["keras_epocas"]
    filas.append(f"Keras en CPU & {len(k)} & {pct(k['test_acc'].mean())} & {pct(k['test_acc'].min())} & "
                 f"{pct(k['test_acc'].max())} & {pct(ke.groupby('corrida')['val_acc'].max().mean())} \\\\")
    for objetivo, nombre in nombres.items():
        g = d["entr"][d["entr"]["objetivo"] == objetivo]
        mejor = d["epocas"][d["epocas"]["objetivo"] == objetivo].groupby("corrida")["val_acc"].max().mean()
        filas.append(f"{nombre} & {len(g)} & {pct(g['test_acc'].mean())} & {pct(g['test_acc'].min())} & "
                     f"{pct(g['test_acc'].max())} & {pct(mejor)} \\\\")
    escribir("exactitud", filas)


def nombre_corto(kernel: str) -> str:
    claves = ("bn_fw", "bn_bw", "winograd", "implicit_gemm", "wgrad", "dgrad", "fprop", "nchwToNhwc",
              "max_pool", "elementwise", "adam", "multi_tensor", "softmax", "nll", "reduce", "gemm",
              "matmul_tiles", "sumar_sesgo", "sumar_columnas")
    for clave in claves:
        posicion = kernel.find(clave)
        if posicion >= 0:
            corto = re.split(r"[^A-Za-z0-9_]", kernel[posicion:])[0]
            # Quita los restos de la codificación de plantillas de C++ y de Numba.
            corto = re.match(r"^([A-Za-z0-9_]+?)(?=I[A-Z]|I[fid]|B\d|$)", corto).group(1)
            return latex(corto[:34])
    return latex(kernel[:34])


def tablas_kernels(d: dict) -> None:
    filas = []
    for fila in d["categorias"].itertuples():
        filas.append(f"{fila.categoria} & {num(fila.lanzamientos_por_paso, 0)} & {num(fila.porcentaje_tiempo)} \\% \\\\")
    escribir("kernels_categoria", filas)

    tabla = d["kernels"]
    principales = tabla.sort_values("total_us", ascending=False).groupby("categoria").head(1).head(7)
    numba = d["kernels_numba"]
    numba = numba[numba["categoria"] == "Kernel propio de Numba"].sort_values("total_us", ascending=False).head(2)
    filas = []
    for fila in pd.concat([principales, numba]).itertuples():
        grid = fila.grid.strip("[]").replace(" ", "")
        bloque = fila.bloque.strip("[]").replace(" ", "")
        ocupacion = "---" if pd.isna(fila.ocupacion_pct) else f"{fila.ocupacion_pct:.0f} \\%"
        filas.append(f"{fila.categoria} & \\texttt{{{nombre_corto(fila.kernel)}}} & ({grid}) & ({bloque}) & "
                     f"{num(fila.hilos, 0)} & {int(fila.registros_por_hilo)} & {ocupacion} \\\\")
    escribir("kernels_ejemplo", filas)

    v = d["verificacion"]
    descripciones = {
        "forward matmul_tiles": "Forward, $Y = XW^T$",
        "forward sumar_sesgo": "Forward, suma del sesgo",
        "backward matmul_tiles grad_x": "Backward, $dX = dY\\,W$",
        "backward matmul_tiles grad_peso": "Backward, $dW = dY^T X$",
        "backward sumar_columnas grad_sesgo": "Backward, $db$ por columnas",
    }
    filas = []
    for clave, (grid, bloque) in v["lanzamientos"].items():
        hilos = 1
        for valor in (*grid, *bloque):
            hilos *= valor
        filas.append(f"{descripciones[clave]} & \\texttt{{{latex(clave.split()[1])}}} & "
                     f"({','.join(map(str, grid))}) & ({','.join(map(str, bloque))}) & {num(hilos, 0)} \\\\")
    escribir("numba_lanzamientos", filas)
    dif = v["diferencia_maxima"]
    (SALIDA / "numba_error.tex").write_text(f"{max(dif.values()):.1e}".replace("e-0", "\\times 10^{-") + "}",
                                            encoding="utf-8")


def tabla_transferencia(d: dict) -> None:
    filas = []
    for memoria, grupo in d["trans"].groupby("memoria"):
        filas.append(f"{memoria.capitalize()} & {num(grupo['megabytes'].iloc[0], 1)} & {pm(grupo['segundos'], 3)} & "
                     f"{num(grupo['gb_por_s'].mean())} \\\\")
    escribir("transferencia", filas)


def main() -> None:
    SALIDA.mkdir(parents=True, exist_ok=True)
    d = cargar()
    tabla_secuencial(d)
    tabla_preparacion(d)
    tabla_comparativa(d)
    tabla_speedup(d)
    tabla_exactitud(d)
    tablas_kernels(d)
    tabla_transferencia(d)
    for archivo in sorted(SALIDA.glob("*.tex")):
        print(f"--- {archivo.name}")
        print(archivo.read_text(encoding="utf-8"))


if __name__ == "__main__":
    main()
