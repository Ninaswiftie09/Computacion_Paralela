
"""Entrenamiento de referencia en CPU con la misma CNN2 y configuración CUDA."""
from __future__ import annotations

import argparse
import json
import platform
from pathlib import Path

import pandas as pd
import torch

from .gpu_training import HP, cargar_datos, resumir, train_once
from .utils import PROJECT_ROOT


def project_path(value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else PROJECT_ROOT / path


def main() -> None:
    parser = argparse.ArgumentParser(description="Entrenamiento CNN2 de referencia en CPU")
    parser.add_argument("--data", default="results/asl_prepared.npz")
    parser.add_argument("--output", default="results/cpu_training_times.json")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--epochs", type=int, default=HP["epochs"])
    parser.add_argument("--batch-size", type=int, default=HP["batch_size"])
    parser.add_argument("--threads", type=int, default=0, help="0 conserva la configuración de PyTorch")
    args = parser.parse_args()

    if min(args.runs, args.epochs, args.batch_size) < 1 or args.threads < 0:
        parser.error("Las ejecuciones, épocas y tamaño de lote deben ser positivos.")
    if args.threads:
        torch.set_num_threads(args.threads)

    source = project_path(args.data)
    if not source.exists():
        parser.error(f"No existe {source}. Ejecutá primero el preprocesamiento.")

    data = cargar_datos(str(source))
    required = ("X_train", "X_val", "X_test", "y_train", "y_val", "y_test")
    if any(key not in data for key in required):
        parser.error("El NPZ no contiene los seis arreglos requeridos.")
    for split in ("train", "val", "test"):
        if not len(data[f"y_{split}"]) or len(data[f"X_{split}"]) != len(data[f"y_{split}"]):
            parser.error(f"El conjunto {split} está vacío o tiene tamaños inconsistentes.")
    if data["X_train"].dtype != torch.float32 or data["X_train"].shape[1:] != (3, 64, 64):
        parser.error("Para esta referencia utilizá --format float32 en el preprocesamiento.")

    hp = {**HP, "epochs": args.epochs, "batch_size": args.batch_size}
    device = torch.device("cpu")
    print("Calentamiento (no se incluye en las mediciones).")
    small = {key: value[:512] for key, value in data.items()}
    train_once(small, {**hp, "epochs": 1}, device, "directo", False)

    runs = []
    for number in range(1, args.runs + 1):
        print(f"Ejecución CPU {number}/{args.runs}")
        runs.append(train_once(data, hp, device, "directo", False))

    summary = resumir(runs)
    summary.update({
        "config": hp,
        "device": "cpu",
        "mode": "directo",
        "torch_version": torch.__version__,
        "cpu_threads": torch.get_num_threads(),
        "system": platform.platform(),
        "split_counts": {split: len(data[f"y_{split}"]) for split in ("train", "val", "test")},
    })
    target = project_path(args.output)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    rows = [{"run": i, **{key: value for key, value in result.items() if key != "historial"}}
            for i, result in enumerate(runs, start=1)]
    pd.DataFrame(rows).to_csv(target.with_suffix(".csv"), index=False)
    print(f"Resultados: {target}")
    print(f"Entrenamiento promedio: {summary['train_s']['mean']:.3f}s")
    print(f"Total promedio: {summary['total_s']['mean']:.3f}s")


if __name__ == "__main__":
    main()
