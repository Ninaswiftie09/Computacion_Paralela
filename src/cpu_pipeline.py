
"""Preparación reproducible del conjunto ASL en CPU para entrenamientos comparables."""
from __future__ import annotations

import argparse
import json
import statistics
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
import pandas as pd
from PIL import Image

from .preprocess_images import create_split_manifest
from .utils import PROJECT_ROOT

SPLITS = {"train": "train", "val": "validation", "test": "test"}
SEED = 42
IMAGE_SIZE = 64


def project_path(value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else PROJECT_ROOT / path


def load_image(image_path: str, image_size: int) -> np.ndarray:
    """Abre un JPG, lo convierte a RGB y lo redimensiona con LANCZOS, igual que el Lab 3."""
    with Image.open(image_path) as image:
        resized = image.convert("RGB").resize((image_size, image_size), Image.Resampling.LANCZOS)
        return np.asarray(resized, dtype=np.uint8)


def load_batch(task: tuple[list[str], int]) -> np.ndarray:
    """Trabajo de un proceso: prepara un lote de imágenes y devuelve sus píxeles en uint8."""
    paths, image_size = task
    return np.stack([load_image(path, image_size) for path in paths])


def _ready(_: int) -> int:
    return 0


def start_pool(workers: int) -> ProcessPoolExecutor:
    """Crea el pool y espera a que todos los procesos estén listos, para medir el arranque aparte."""
    pool = ProcessPoolExecutor(max_workers=workers)
    list(pool.map(_ready, range(workers * 4)))
    return pool


def prepare_arrays(manifest: pd.DataFrame, image_size: int, data_format: str,
                   workers: int = 1, batch: int = 200,
                   pool: ProcessPoolExecutor | None = None) -> dict:
    """Lee, decodifica, redimensiona y prepara las imágenes sin utilizar CUDA.

    Con workers igual a 1 las imágenes se procesan una por una en un solo núcleo. Con más
    workers se reparten en lotes entre procesos independientes, uno por núcleo. Cada imagen
    se procesa por separado, así que el resultado es idéntico en ambos casos. Si se recibe
    un pool ya iniciado se reutiliza y no se cierra.
    """
    classes = sorted(manifest["class_name"].unique().tolist())
    if len(classes) != 29:
        raise ValueError(f"Se esperaban 29 clases ASL, pero se encontraron {len(classes)}.")
    label_by_class = {name: index for index, name in enumerate(classes)}
    arrays: dict[str, np.ndarray] = {"clases": np.asarray(classes)}

    own_pool = pool is None and workers > 1
    if own_pool:
        pool = start_pool(workers)
    try:
        for target, source in SPLITS.items():
            rows = manifest.loc[manifest["split"] == source]
            if rows.empty:
                raise ValueError(f"El conjunto {source} no contiene imágenes.")
            paths = [str(PROJECT_ROOT / path) for path in rows["source_path"]]

            if pool is None:
                pixels = np.stack([load_image(path, image_size) for path in paths])
            else:
                tasks = [(paths[i:i + batch], image_size) for i in range(0, len(paths), batch)]
                pixels = np.concatenate(list(pool.map(load_batch, tasks)))

            if data_format == "float32":
                # División entre 255, como en el Lab 3, y orden NCHW para PyTorch.
                x = np.ascontiguousarray(pixels.transpose(0, 3, 1, 2), dtype=np.float32)
                x /= 255.0
            else:
                x = pixels
            arrays[f"X_{target}"] = x
            arrays[f"y_{target}"] = rows["class_name"].map(label_by_class).to_numpy(dtype=np.int64)
    finally:
        if own_pool:
            pool.shutdown(wait=True, cancel_futures=True)

    return arrays


def describe(runs: list[dict], key: str) -> dict:
    values = [row[key] for row in runs]
    return {
        "mean": statistics.mean(values),
        "std": statistics.stdev(values) if len(values) > 1 else 0.0,
        "runs": values,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Preprocesamiento ASL en CPU")
    parser.add_argument("--images-per-class", type=int, default=600)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--format", choices=["float32", "uint8"], default="float32")
    parser.add_argument("--output", default="results/asl_prepared.npz")
    parser.add_argument("--results-dir", default="results")
    parser.add_argument("--workers", type=int, default=1,
                        help="Procesos para preparar las imágenes, 1 equivale a un solo núcleo")
    args = parser.parse_args()

    if args.runs < 1 or args.images_per_class < 10:
        parser.error("Usá al menos una ejecución y diez imágenes por clase.")

    output = project_path(args.output)
    results_dir = project_path(args.results_dir)
    output.parent.mkdir(parents=True, exist_ok=True)
    results_dir.mkdir(parents=True, exist_ok=True)
    records = []
    counts = {}

    for run in range(1, args.runs + 1):
        start = time.perf_counter()
        manifest = create_split_manifest(images_per_class=args.images_per_class, seed=SEED)
        split_s = time.perf_counter() - start

        start_prepare = time.perf_counter()
        arrays = prepare_arrays(manifest, IMAGE_SIZE, args.format, workers=args.workers)
        prepare_s = time.perf_counter() - start_prepare

        start_save = time.perf_counter()
        np.savez_compressed(output, **arrays)
        save_s = time.perf_counter() - start_save
        total_s = split_s + prepare_s + save_s

        counts = {name: int(len(arrays[f"y_{name}"])) for name in SPLITS}
        records.append({
            "run": run,
            "split_s": split_s,
            "prepare_s": prepare_s,
            "save_s": save_s,
            "total_s": total_s,
            "ms_per_image": 1000 * prepare_s / sum(counts.values()),
        })
        print(
            f"Ejecución {run}/{args.runs}: "
            f"selección={split_s:.3f}s, preparación={prepare_s:.3f}s, "
            f"guardado={save_s:.3f}s, total={total_s:.3f}s"
        )

    manifest.to_csv(results_dir / "asl_split_manifest.csv", index=False)
    summary = {
        "dataset": str(output.relative_to(PROJECT_ROOT)) if output.is_relative_to(PROJECT_ROOT) else str(output),
        "seed": SEED,
        "images_per_class": args.images_per_class,
        "image_size": IMAGE_SIZE,
        "format": args.format,
        "workers": args.workers,
        "classes": arrays["clases"].tolist(),
        "split_counts": counts,
        "metrics": {key: describe(records, key) for key in
                    ("split_s", "prepare_s", "save_s", "total_s", "ms_per_image")},
    }
    (results_dir / "cpu_preprocessing_times.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8"
    )
    pd.DataFrame(records).to_csv(results_dir / "cpu_preprocessing_times.csv", index=False)
    print(f"Archivo preparado: {output}")
    print(f"Imágenes por conjunto: {counts}")


if __name__ == "__main__":
    main()
