"""Medición del código original del Lab 3 (Keras, CPU) para la familia cnn_improved.

Parte original: llama sin cambios a tune_keras_family de src/model_training.py, que lee los
JPEG con tf.data, entrena las dos configuraciones de cnn_improved por 3 épocas con los
callbacks del laboratorio y elige la mejor por exactitud de validación.

Parte validacion: entrena la configuración elegida con los arreglos ya preparados en CPU
(los mismos que usa PyTorch), sin callbacks y con épocas fijas. Sirve para comprobar que la
traducción a PyTorch aprende igual y para comparar tiempos con el mismo trabajo.

Uso:
  python -m src.keras_referencia original
  python -m src.keras_referencia validacion --config cnn_improved_b --npz RUTA --runs 2
"""
from __future__ import annotations

import argparse
import json
import platform
import time
from pathlib import Path

import numpy as np
import pandas as pd
import tensorflow as tf

from . import model_training as lab
from .utils import PROJECT_ROOT

SALIDA = PROJECT_ROOT / "results" / "keras_referencia"


class TiempoPorEpoca(tf.keras.callbacks.Callback):
    def on_train_begin(self, logs=None):
        self.tiempos = []

    def on_epoch_begin(self, epoch, logs=None):
        self.inicio = time.perf_counter()

    def on_epoch_end(self, epoch, logs=None):
        self.tiempos.append(time.perf_counter() - self.inicio)


def entorno() -> dict:
    return dict(tensorflow=tf.__version__, sistema=platform.platform(),
                dispositivos=[d.name for d in tf.config.list_physical_devices()],
                hilos_intra=tf.config.threading.get_intra_op_parallelism_threads(),
                hilos_inter=tf.config.threading.get_inter_op_parallelism_threads())


def parte_original(epocas: int) -> None:
    lab.ensure_model_directories()
    tf.keras.utils.set_random_seed(lab.RANDOM_SEED)
    manifest = lab.load_manifest()
    clases = lab.get_class_names(manifest)
    inicio = time.perf_counter()
    tabla, mejor, _ = lab.tune_keras_family(
        family="cnn_improved", configs=lab.CNN_IMPROVED_CONFIGS, manifest=manifest,
        class_names=clases, epochs=epocas, batch_size=lab.DEFAULT_BATCH_SIZE,
    )
    total = time.perf_counter() - inicio
    tabla.to_csv(SALIDA / "ajuste_cnn_improved.csv", index=False)
    resumen = dict(entorno=entorno(), epocas=epocas, batch_size=lab.DEFAULT_BATCH_SIZE,
                   imagenes=int(len(manifest)), mejor_config=mejor, tiempo_total_s=total)
    (SALIDA / "ajuste_cnn_improved.json").write_text(
        json.dumps(resumen, indent=2, ensure_ascii=False), encoding="utf-8")
    print(tabla[["model_name", "training_time_seconds", "epochs_completed",
                 "validation_accuracy"]].to_string(index=False))
    print(f"Mejor configuración: {mejor['name']}")


def parte_validacion(nombre: str, npz: Path, corridas: int, epocas: int) -> None:
    config = next(c for c in lab.CNN_IMPROVED_CONFIGS if c["name"] == nombre)
    datos = np.load(npz)
    # Los arreglos vienen en NCHW para PyTorch. Keras espera NHWC.
    X = {k: np.ascontiguousarray(datos[f"X_{k}"].transpose(0, 2, 3, 1)) for k in ("train", "val", "test")}
    y = {k: datos[f"y_{k}"].astype("int32") for k in ("train", "val", "test")}
    filas, epocas_filas = [], []
    for corrida in range(1, corridas + 1):
        tf.keras.backend.clear_session()
        tf.keras.utils.set_random_seed(lab.RANDOM_SEED)
        modelo = lab.build_cnn_improved(29, config)
        tiempos = TiempoPorEpoca()
        inicio = time.perf_counter()
        historia = modelo.fit(X["train"], y["train"], validation_data=(X["val"], y["val"]),
                              epochs=epocas, batch_size=lab.DEFAULT_BATCH_SIZE, shuffle=True,
                              callbacks=[tiempos], verbose=2)
        entrenamiento = time.perf_counter() - inicio
        inicio = time.perf_counter()
        _, test_acc = modelo.evaluate(X["test"], y["test"], batch_size=512, verbose=0)
        prueba = time.perf_counter() - inicio
        filas.append(dict(corrida=corrida, train_s=entrenamiento, test_s=prueba,
                          epoch_mean_s=float(np.mean(tiempos.tiempos)),
                          val_acc_final=float(historia.history["val_accuracy"][-1]),
                          test_acc=float(test_acc), parametros=int(modelo.count_params())))
        for i, acc in enumerate(historia.history["val_accuracy"], start=1):
            epocas_filas.append(dict(corrida=corrida, epoca=i, val_acc=float(acc),
                                     segundos=tiempos.tiempos[i - 1]))
        print(f"Corrida {corrida}: entrenamiento={entrenamiento:.1f}s test_acc={test_acc:.4f}")
    pd.DataFrame(filas).to_csv(SALIDA / "validacion_keras_cpu.csv", index=False)
    pd.DataFrame(epocas_filas).to_csv(SALIDA / "validacion_keras_epocas.csv", index=False)
    (SALIDA / "validacion_keras_cpu.json").write_text(
        json.dumps(dict(entorno=entorno(), config=config, epocas=epocas, npz=str(npz)),
                   indent=2, ensure_ascii=False), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Referencia Keras del Lab 3 en CPU")
    parser.add_argument("parte", choices=["original", "validacion"])
    parser.add_argument("--epochs", type=int, default=None)
    parser.add_argument("--config", default="cnn_improved_b")
    parser.add_argument("--npz", default="results/asl_prepared.npz")
    parser.add_argument("--runs", type=int, default=2)
    args = parser.parse_args()
    SALIDA.mkdir(parents=True, exist_ok=True)
    if args.parte == "original":
        parte_original(args.epochs or lab.DEFAULT_TUNING_EPOCHS)
    else:
        npz = Path(args.npz)
        parte_validacion(args.config, npz if npz.is_absolute() else PROJECT_ROOT / npz,
                         args.runs, args.epochs or 10)


if __name__ == "__main__":
    main()
