"""Benchmark del pipeline ASL en CPU y GPU bajo las mismas condiciones.

Integrante 3. Usa las funciones de cpu_pipeline.py y gpu_training.py, para que CPU y GPU
ejecuten exactamente el mismo código con los mismos datos, modelo, épocas, lote y semilla.

Fases que se miden:
  Preparación en CPU: selección del split, y lectura y ajuste de imágenes con 1 proceso y
  con varios procesos. Se comprueba con SHA-256 que todas las variantes producen los mismos datos.
  Almacenamiento: guardado y carga del NPZ.
  Transferencia del conjunto completo de RAM a VRAM, con memoria paginable y fijada.
  Entrenamiento (10 épocas con validación) y prueba en CPU, GPU directo, GPU optimizado y
  GPU optimizado con la capa densa hecha con kernels propios de Numba (src/numba_dense.py).

Durante toda la ejecución se registran el uso de cada núcleo del CPU y el uso de la GPU.

Uso:
  python -m src.benchmark --npz C:/Users/usuario/datasets/p3_work/asl_prepared.npz
  python -m src.benchmark --npz ... --prep-runs 1 --runs 1 --epochs 1 --out results/benchmark_prueba
"""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
import time
from pathlib import Path

import numpy as np
import pandas as pd
import psutil

# PyTorch se importa dentro de las funciones. En Windows cada proceso del pool vuelve a
# importar este módulo, y así los procesos que preparan imágenes no cargan PyTorch.
from .cpu_pipeline import IMAGE_SIZE, SEED, prepare_arrays, start_pool
from .monitor import Monitor
from .preprocess_images import create_split_manifest
from .utils import PROJECT_ROOT

OBJETIVOS = {
    "cpu": ("cpu", "directo"),
    "gpu_directo": ("cuda", "directo"),
    "gpu_optimizado": ("cuda", "optimizado"),
    "gpu_numba": ("cuda", "optimizado"),
}


def ruta(valor: str) -> Path:
    path = Path(valor)
    return path if path.is_absolute() else PROJECT_ROOT / path


class Bitacora:
    """Guarda el inicio y fin de cada fase para alinearlas con el monitor."""

    def __init__(self) -> None:
        self.filas: list[dict] = []

    def registrar(self, fase: str, objetivo: str, corrida: int, inicio: float, fin: float) -> None:
        self.filas.append(dict(fase=fase, objetivo=objetivo, corrida=corrida,
                               t_inicio=round(inicio, 3), t_fin=round(fin, 3)))


def huella(arrays: dict) -> str:
    """SHA-256 de imágenes y etiquetas, para comprobar que todas las fases usan los mismos datos."""
    h = hashlib.sha256()
    for clave in sorted(k for k in arrays if k.startswith(("X_", "y_"))):
        valor = arrays[clave]
        valor = valor.numpy() if hasattr(valor, "numpy") else valor
        h.update(clave.encode())
        h.update(np.ascontiguousarray(valor).data)
    return h.hexdigest()


def nombre_cpu() -> str:
    try:
        import winreg
        clave = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                               r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
        return winreg.QueryValueEx(clave, "ProcessorNameString")[0].strip()
    except OSError:
        return platform.processor()


def info_sistema() -> dict:
    import torch

    info = dict(
        sistema=platform.platform(),
        python=platform.python_version(),
        cpu=nombre_cpu(),
        nucleos_fisicos=psutil.cpu_count(logical=False),
        nucleos_logicos=psutil.cpu_count(),
        ram_gb=round(psutil.virtual_memory().total / 2**30, 1),
        torch=torch.__version__,
        hilos_torch=torch.get_num_threads(),
        cuda_disponible=torch.cuda.is_available(),
    )
    if torch.cuda.is_available():
        propiedades = torch.cuda.get_device_properties(0)
        info.update(
            gpu=propiedades.name,
            vram_mb=round(propiedades.total_memory / 2**20),
            sms=propiedades.multi_processor_count,
            compute_capability=f"{propiedades.major}.{propiedades.minor}",
            cuda_torch=torch.version.cuda,
            cudnn=torch.backends.cudnn.version(),
        )
        try:
            salida = subprocess.run(
                ["nvidia-smi", "--query-gpu=driver_version", "--format=csv,noheader"],
                capture_output=True, text=True, timeout=10, check=True,
            )
            info["driver_nvidia"] = salida.stdout.strip()
        except (OSError, subprocess.SubprocessError):
            pass
    return info


def medir_preparacion(lista_workers: list[int], corridas: int, imagenes_por_clase: int,
                      bitacora: Bitacora) -> tuple[list[dict], dict]:
    """Prepara las imágenes con cada cantidad de procesos. La selección se mide con 1 proceso."""
    filas = []
    manifest = None
    arrays = None
    for workers in lista_workers:
        pool, arranque = None, 0.0
        if workers > 1:
            inicio = time.time()
            t = time.perf_counter()
            pool = start_pool(workers)
            arranque = time.perf_counter() - t
            bitacora.registrar("arranque_pool", f"cpu_{workers}", 0, inicio, time.time())
        try:
            for corrida in range(1, corridas + 1):
                inicio = time.time()
                seleccion = float("nan")
                if workers == 1:
                    t = time.perf_counter()
                    manifest = create_split_manifest(images_per_class=imagenes_por_clase, seed=SEED)
                    seleccion = time.perf_counter() - t
                t = time.perf_counter()
                arrays = prepare_arrays(manifest, IMAGE_SIZE, "float32", workers=workers, pool=pool)
                preparacion = time.perf_counter() - t
                fin = time.time()
                imagenes = sum(len(arrays[k]) for k in arrays if k.startswith("y_"))
                bitacora.registrar("preparacion", f"cpu_{workers}", corrida, inicio, fin)
                filas.append(dict(workers=workers, corrida=corrida, seleccion_s=seleccion,
                                  arranque_pool_s=arranque, preparacion_s=preparacion,
                                  ms_por_imagen=1000 * preparacion / imagenes, huella=huella(arrays)))
                extra = (f" (selección {seleccion:.2f}s)" if workers == 1
                         else f" (arranque del pool {arranque:.2f}s)")
                print(f"Preparación con {workers:2d} procesos, corrida {corrida}/{corridas}: "
                      f"{preparacion:.2f}s{extra}")
        finally:
            if pool is not None:
                pool.shutdown(wait=True, cancel_futures=True)
    return filas, arrays


def medir_almacenamiento(arrays: dict, corridas: int, npz: Path,
                         bitacora: Bitacora) -> tuple[list[dict], dict]:
    from .gpu_training import cargar_datos

    filas = []
    datos = None
    for corrida in range(1, corridas + 1):
        inicio = time.time()
        t = time.perf_counter()
        np.savez_compressed(npz, **arrays)
        guardado = time.perf_counter() - t
        t = time.perf_counter()
        datos = cargar_datos(str(npz))
        carga = time.perf_counter() - t
        bitacora.registrar("almacenamiento", "cpu", corrida, inicio, time.time())
        filas.append(dict(corrida=corrida, guardado_s=guardado, carga_s=carga,
                          megabytes_npz=npz.stat().st_size / 2**20))
        print(f"Almacenamiento {corrida}/{corridas}: guardado={guardado:.2f}s carga={carga:.2f}s")
    return filas, datos


def medir_transferencia(datos: dict, repeticiones: int, bitacora: Bitacora) -> list[dict]:
    """Copia el conjunto completo a la GPU con memoria paginable y con memoria fijada."""
    import torch

    dispositivo = torch.device("cuda")
    filas = []
    bytes_totales = sum(v.numel() * v.element_size() for v in datos.values())
    for memoria in ("paginable", "fijada"):
        origen = {k: (v.pin_memory() if memoria == "fijada" else v) for k, v in datos.items()}
        asincrona = memoria == "fijada"
        for repeticion in range(repeticiones + 1):
            torch.cuda.synchronize()
            inicio = time.time()
            t = time.perf_counter()
            copia = {k: v.to(dispositivo, non_blocking=asincrona) for k, v in origen.items()}
            torch.cuda.synchronize()
            segundos = time.perf_counter() - t
            fin = time.time()
            del copia
            torch.cuda.empty_cache()
            if repeticion == 0:
                continue
            bitacora.registrar("transferencia", f"h2d_{memoria}", repeticion, inicio, fin)
            filas.append(dict(memoria=memoria, repeticion=repeticion, segundos=segundos,
                              megabytes=bytes_totales / 2**20,
                              gb_por_s=bytes_totales / segundos / 1e9))
        del origen
        valores = [f["segundos"] for f in filas if f["memoria"] == memoria]
        print(f"Transferencia {memoria}: {sum(valores) / len(valores):.3f}s por copia")
    return filas


def medir_entrenamiento(datos: dict, objetivos: list[str], corridas: int, hp: dict,
                        bitacora: Bitacora) -> tuple[list[dict], list[dict]]:
    import torch
    from .gpu_training import train_once

    filas, epocas = [], []
    pequeno = {k: v[:512] for k, v in datos.items()}
    for objetivo in objetivos:
        tipo, modo = OBJETIVOS[objetivo]
        dispositivo = torch.device(tipo)
        torch.backends.cudnn.benchmark = tipo == "cuda"
        fabrica = None
        if objetivo == "gpu_numba":
            from .numba_dense import crear_modelo_numba
            fabrica = crear_modelo_numba
        print(f"\n[{objetivo}] calentamiento de 1 época con 512 imágenes (no se mide)")
        train_once(pequeno, {**hp, "epochs": 1}, dispositivo, modo, False, modelo=fabrica)
        for corrida in range(1, corridas + 1):
            print(f"[{objetivo}] corrida {corrida}/{corridas}")
            inicio = time.time()
            resultado = train_once(datos, hp, dispositivo, modo, False, modelo=fabrica)
            fin = time.time()
            bitacora.registrar("entrenamiento", objetivo, corrida, inicio, fin)
            historial = resultado.pop("historial")
            filas.append(dict(objetivo=objetivo, dispositivo=tipo, modo=modo,
                              corrida=corrida, **resultado))
            for epoca in historial:
                epocas.append(dict(objetivo=objetivo, corrida=corrida, **epoca))
        if tipo == "cuda":
            torch.cuda.empty_cache()
    return filas, epocas


def main() -> None:
    import torch
    from .gpu_training import CNN_IMPROVED, HP

    parser = argparse.ArgumentParser(description="Benchmark CPU contra GPU del pipeline ASL")
    parser.add_argument("--npz", default="results/asl_prepared.npz",
                        help="Conviene una ruta fuera de OneDrive para que la sincronización no afecte el guardado")
    parser.add_argument("--out", default="results/benchmark")
    parser.add_argument("--images-per-class", type=int, default=600)
    parser.add_argument("--workers", type=int, nargs="+", default=[1, 2, 4, 8, 16, 24, 32],
                        help="Cantidades de procesos para la preparación de imágenes")
    parser.add_argument("--prep-runs", type=int, default=3)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--epochs", type=int, default=HP["epochs"])
    parser.add_argument("--config", default=HP["config"], choices=list(CNN_IMPROVED))
    parser.add_argument("--transfer-reps", type=int, default=5)
    parser.add_argument("--targets", nargs="+", choices=list(OBJETIVOS), default=list(OBJETIVOS))
    args = parser.parse_args()

    if min(args.prep_runs, args.runs, args.epochs, args.transfer_reps, *args.workers) < 1:
        parser.error("Las corridas, épocas, repeticiones y procesos deben ser positivos.")
    usa_gpu = any(OBJETIVOS[o][0] == "cuda" for o in args.targets)
    if usa_gpu and not torch.cuda.is_available():
        parser.error("CUDA no está disponible. Use --targets cpu o revise el driver.")

    lista_workers = sorted(set(args.workers) | {1})
    salida = ruta(args.out)
    salida.mkdir(parents=True, exist_ok=True)
    npz = ruta(args.npz)
    npz.parent.mkdir(parents=True, exist_ok=True)
    hp = {**HP, "epochs": args.epochs, "config": args.config, "lr": CNN_IMPROVED[args.config]["lr"]}
    torch.manual_seed(SEED)
    if usa_gpu:
        torch.zeros(1, device="cuda")

    sistema = info_sistema()
    configuracion = dict(hp=hp, modelo=CNN_IMPROVED[args.config], seed=SEED, image_size=IMAGE_SIZE,
                         images_per_class=args.images_per_class, workers=lista_workers,
                         prep_runs=args.prep_runs, runs=args.runs,
                         transfer_reps=args.transfer_reps, targets=args.targets, npz=str(npz),
                         amp=False, cudnn_benchmark_en_gpu=True)
    print(json.dumps(sistema, indent=2, ensure_ascii=False))

    bitacora = Bitacora()
    inicio_total = time.time()
    try:
        with Monitor(salida, gpu=usa_gpu):
            preparacion, arrays = medir_preparacion(lista_workers, args.prep_runs,
                                                    args.images_per_class, bitacora)
            almacenamiento, datos = medir_almacenamiento(arrays, args.prep_runs, npz, bitacora)
            del arrays
            configuracion["huella_datos"] = huella(datos)
            configuracion["imagenes"] = {k[2:]: len(v) for k, v in datos.items() if k.startswith("y_")}
            transferencia = medir_transferencia(datos, args.transfer_reps, bitacora) if usa_gpu else []
            entrenamiento, epocas = medir_entrenamiento(datos, args.targets, args.runs, hp, bitacora)
    finally:
        if usa_gpu:
            torch.cuda.empty_cache()

    configuracion["duracion_total_s"] = round(time.time() - inicio_total, 1)
    pd.DataFrame(preparacion).to_csv(salida / "corridas_preparacion.csv", index=False)
    pd.DataFrame(almacenamiento).to_csv(salida / "corridas_almacenamiento.csv", index=False)
    pd.DataFrame(transferencia).to_csv(salida / "transferencias.csv", index=False)
    pd.DataFrame(entrenamiento).to_csv(salida / "corridas_entrenamiento.csv", index=False)
    pd.DataFrame(epocas).to_csv(salida / "epocas.csv", index=False)
    pd.DataFrame(bitacora.filas).to_csv(salida / "fases.csv", index=False)
    (salida / "sistema.json").write_text(
        json.dumps(dict(sistema=sistema, configuracion=configuracion), indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    print(f"\nBenchmark terminado en {configuracion['duracion_total_s']} s. Resultados en {salida}")


if __name__ == "__main__":
    main()
