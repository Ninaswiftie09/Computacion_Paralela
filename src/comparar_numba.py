"""Compara el modo optimizado con la variante de kernels Numba en corridas alternadas.

En el benchmark las variantes corren en bloques, una después de otra. Aquí se intercalan
para que la temperatura de la GPU y el orden de ejecución no favorezcan a ninguna.

Uso:
  python -m src.comparar_numba --npz C:/Users/usuario/datasets/p3_work/asl_prepared.npz
"""
from __future__ import annotations

import argparse
import time
from pathlib import Path

import pandas as pd
import torch

from .gpu_training import HP, PROJECT_ROOT, cargar_datos, train_once
from .monitor import Monitor
from .numba_dense import crear_modelo_numba


def main() -> None:
    parser = argparse.ArgumentParser(description="Modo optimizado contra kernels Numba")
    parser.add_argument("--npz", default=str(PROJECT_ROOT / "results" / "asl_prepared.npz"))
    parser.add_argument("--runs", type=int, default=4)
    parser.add_argument("--out", default=str(PROJECT_ROOT / "results" / "perfil_gpu"))
    args = parser.parse_args()
    salida = Path(args.out)
    salida.mkdir(parents=True, exist_ok=True)

    datos = cargar_datos(args.npz)
    dispositivo = torch.device("cuda")
    torch.backends.cudnn.benchmark = True
    variantes = {"gpu_optimizado": None, "gpu_numba": crear_modelo_numba}
    pequeno = {k: v[:512] for k, v in datos.items()}
    for fabrica in variantes.values():
        train_once(pequeno, {**HP, "epochs": 1}, dispositivo, "optimizado", False, modelo=fabrica)

    filas = []
    with Monitor(salida, gpu=True, prefijo="comparacion_numba_monitor"):
        for corrida in range(1, args.runs + 1):
            for nombre, fabrica in variantes.items():
                inicio = time.time()
                r = train_once(datos, HP, dispositivo, "optimizado", False, modelo=fabrica)
                filas.append(dict(objetivo=nombre, corrida=corrida, t_inicio=inicio, t_fin=time.time(),
                                  train_s=r["train_s"], epoch_mean_s=r["epoch_mean_s"], test_acc=r["test_acc"]))
                print(f"{nombre} corrida {corrida}: {r['train_s']:.2f} s")
    tabla = pd.DataFrame(filas)
    tabla.to_csv(salida / "comparacion_numba.csv", index=False)
    print(tabla.groupby("objetivo")["train_s"].agg(["mean", "std", "median", "min", "max"]).round(3))


if __name__ == "__main__":
    main()
