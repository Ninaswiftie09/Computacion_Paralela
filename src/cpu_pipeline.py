
"""Preparación reproducible del conjunto ASL en CPU para entrenamientos comparables."""
from __future__ import annotations

import argparse
import json
import statistics
import time
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


def prepare_arrays(manifest: pd.DataFrame, image_size: int, data_format: str) -> dict:
    """Lee, decodifica, redimensiona y prepara las imágenes sin utilizar CUDA."""
    classes = sorted(manifest["class_name"].unique().tolist())
    if len(classes) != 29:
        raise ValueError(f"Se esperaban 29 clases ASL, pero se encontraron {len(classes)}.")
    label_by_class = {name: index for index, name in enumerate(classes)}
    arrays: dict[str, np.ndarray] = {"clases": np.asarray(classes)}

    for target, source in SPLITS.items():
        rows = manifest.loc[manifest["split"] == source]
        if rows.empty:
            raise ValueError(f"El conjunto {source} no contiene imágenes.")

        if data_format == "float32":
            x = np.empty((len(rows), 3, image_size, image_size), dtype=np.float32)
        else:
            x = np.empty((len(rows), image_size, image_size, 3), dtype=np.uint8)
        y = np.empty(len(rows), dtype=np.int64)

        for index, row in enumerate(rows.itertuples(index=False)):
            image_path = PROJECT_ROOT / row.source_path
            with Image.open(image_path) as image:
                resized = image.convert("RGB").resize(
                    (image_size, image_size), Image.Resampling.LANCZOS
                )
                pixels = np.asarray(resized, dtype=np.uint8)

            if data_format == "float32":
                # Normalización equivalente a (pixel / 255 - 0.5) / 0.5.
                x[index] = ((pixels.astype(np.float32) / 255.0 - 0.5) / 0.5).transpose(2, 0, 1)
            else:
                x[index] = pixels
            y[index] = label_by_class[row.class_name]

        arrays[f"X_{target}"] = x
        arrays[f"y_{target}"] = y

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
        arrays = prepare_arrays(manifest, IMAGE_SIZE, args.format)
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
