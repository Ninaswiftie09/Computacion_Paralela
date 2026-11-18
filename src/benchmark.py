"""Benchmark del pipeline ASL en CPU y GPU bajo las mismas condiciones.

Integrante 3. Usa las funciones de cpu_pipeline.py (Integrante 1) y gpu_training.py
(Integrante 2) sin modificarlas, para que CPU y GPU ejecuten exactamente el mismo código.

Fases que se miden:
  Preparación en CPU, común a las dos versiones: selección del split, lectura y ajuste
  de imágenes, guardado del NPZ y carga del NPZ.
  Transferencia del conjunto completo de RAM a VRAM, con memoria paginable y fijada.
  Entrenamiento (10 épocas con validación) y prueba en CPU, GPU directo y GPU optimizado.

Durante toda la ejecución se registran el uso de cada núcleo del CPU y el uso de la GPU.

Uso:
  python -m src.benchmark --npz C:/Users/usuario/datasets/p3_work/asl_prepared.npz
  python -m src.benchmark --npz ... --prep-runs 1 --runs 1 --epochs 1 --out results/benchmark_prueba
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import platform
import subprocess
import threading
import time
from pathlib import Path

import numpy as np
import pandas as pd
import psutil
import torch

from .cpu_pipeline import IMAGE_SIZE, SEED, prepare_arrays
from .gpu_training import HP, cargar_datos, train_once
from .preprocess_images import create_split_manifest
from .utils import PROJECT_ROOT

OBJETIVOS = {
    "cpu": ("cpu", "directo"),
    "gpu_directo": ("cuda", "directo"),
    "gpu_optimizado": ("cuda", "optimizado"),
}
CONSULTA_GPU = (
    "utilization.gpu,utilization.memory,memory.used,memory.total,"
    "temperature.gpu,power.draw,clocks.sm"
)


def ruta(valor: str) -> Path:
    path = Path(valor)
    return path if path.is_absolute() else PROJECT_ROOT / path


class Monitor:
    """Registra cada segundo el uso por núcleo del CPU y el estado de la GPU.

    Corre en un hilo daemon que se detiene al salir del bloque with, incluso con errores.
    nvidia-smi se llama una vez por muestra con tiempo límite, así no queda ningún
    proceso hijo vivo si el programa se interrumpe.
    """

    def __init__(self, carpeta: Path, intervalo: float = 1.0, gpu: bool = True):
        self.cpu_path = carpeta / "monitor_cpu.csv"
        self.gpu_path = carpeta / "monitor_gpu.csv"
        self.intervalo = intervalo
        self.gpu = gpu
        self.detener = threading.Event()
        self.hilo = threading.Thread(target=self._ciclo, name="monitor", daemon=True)
        self.proceso = psutil.Process()

    def __enter__(self) -> "Monitor":
        psutil.cpu_percent(percpu=True)
        self.hilo.start()
        return self

    def __exit__(self, *_) -> None:
        self.detener.set()
        self.hilo.join(timeout=10)

    def _muestra_gpu(self) -> list[str] | None:
        try:
            salida = subprocess.run(
                ["nvidia-smi", f"--query-gpu={CONSULTA_GPU}", "--format=csv,noheader,nounits"],
                capture_output=True, text=True, timeout=5, check=True,
            )
        except (OSError, subprocess.SubprocessError):
            return None
        return [valor.strip() for valor in salida.stdout.strip().splitlines()[0].split(",")]

    def _ciclo(self) -> None:
        nucleos = psutil.cpu_count()
        with open(self.cpu_path, "w", newline="") as f_cpu, \
             open(self.gpu_path, "w", newline="") as f_gpu:
            cpu = csv.writer(f_cpu)
            gpu = csv.writer(f_gpu)
            cpu.writerow(["t", "cpu_total", "ram_proceso_mb",
                          *[f"nucleo_{i}" for i in range(nucleos)]])
            gpu.writerow(["t", *CONSULTA_GPU.split(",")])
            while not self.detener.wait(self.intervalo):
                ahora = time.time()
                por_nucleo = psutil.cpu_percent(percpu=True)
                try:
                    ram = self.proceso.memory_info().rss / 2**20
                except psutil.Error:
                    ram = float("nan")
                cpu.writerow([f"{ahora:.3f}", f"{sum(por_nucleo) / len(por_nucleo):.1f}",
                              f"{ram:.1f}", *por_nucleo])
                f_cpu.flush()
                if self.gpu:
                    valores = self._muestra_gpu()
                    if valores:
                        gpu.writerow([f"{ahora:.3f}", *valores])
                        f_gpu.flush()


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
        valor = valor.numpy() if isinstance(valor, torch.Tensor) else valor
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


def medir_preparacion(corridas: int, imagenes_por_clase: int, npz: Path,
                      bitacora: Bitacora) -> tuple[list[dict], dict]:
    filas = []
    datos = None
    for corrida in range(1, corridas + 1):
        inicio = time.time()
        t = time.perf_counter()
        manifest = create_split_manifest(images_per_class=imagenes_por_clase, seed=SEED)
        seleccion = time.perf_counter() - t

        t = time.perf_counter()
        arrays = prepare_arrays(manifest, IMAGE_SIZE, "float32")
        preparacion = time.perf_counter() - t
        firma = huella(arrays)

        t = time.perf_counter()
        np.savez_compressed(npz, **arrays)
        guardado = time.perf_counter() - t
        del arrays

        t = time.perf_counter()
        datos = cargar_datos(str(npz))
        carga = time.perf_counter() - t
        fin = time.time()

        bitacora.registrar("preparacion", "cpu", corrida, inicio, fin)
        imagenes = sum(len(datos[k]) for k in datos if k.startswith("y_"))
        filas.append(dict(corrida=corrida, seleccion_s=seleccion, preparacion_s=preparacion,
                          guardado_s=guardado, carga_s=carga,
                          total_s=seleccion + preparacion + guardado + carga,
                          ms_por_imagen=1000 * preparacion / imagenes, huella=firma))
        print(f"Preparación {corrida}/{corridas}: selección={seleccion:.2f}s "
              f"preparación={preparacion:.2f}s guardado={guardado:.2f}s carga={carga:.2f}s")
    return filas, datos


def medir_transferencia(datos: dict, repeticiones: int, bitacora: Bitacora) -> list[dict]:
    """Copia el conjunto completo a la GPU con memoria paginable y con memoria fijada."""
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
    filas, epocas = [], []
    pequeno = {k: v[:512] for k, v in datos.items()}
    for objetivo in objetivos:
        tipo, modo = OBJETIVOS[objetivo]
        dispositivo = torch.device(tipo)
        torch.backends.cudnn.benchmark = tipo == "cuda"
        print(f"\n[{objetivo}] calentamiento de 1 época con 512 imágenes (no se mide)")
        train_once(pequeno, {**hp, "epochs": 1}, dispositivo, modo, False)
        for corrida in range(1, corridas + 1):
            print(f"[{objetivo}] corrida {corrida}/{corridas}")
            inicio = time.time()
            resultado = train_once(datos, hp, dispositivo, modo, False)
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
    parser = argparse.ArgumentParser(description="Benchmark CPU contra GPU del pipeline ASL")
    parser.add_argument("--npz", default="results/asl_prepared.npz",
                        help="Conviene una ruta fuera de OneDrive para que la sincronización no afecte el guardado")
    parser.add_argument("--out", default="results/benchmark")
    parser.add_argument("--images-per-class", type=int, default=600)
    parser.add_argument("--prep-runs", type=int, default=3)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--epochs", type=int, default=HP["epochs"])
    parser.add_argument("--transfer-reps", type=int, default=5)
    parser.add_argument("--targets", nargs="+", choices=list(OBJETIVOS), default=list(OBJETIVOS))
    args = parser.parse_args()

    if min(args.prep_runs, args.runs, args.epochs, args.transfer_reps) < 1:
        parser.error("Las corridas, épocas y repeticiones deben ser positivas.")
    usa_gpu = any(OBJETIVOS[o][0] == "cuda" for o in args.targets)
    if usa_gpu and not torch.cuda.is_available():
        parser.error("CUDA no está disponible. Use --targets cpu o revise el driver.")

    salida = ruta(args.out)
    salida.mkdir(parents=True, exist_ok=True)
    npz = ruta(args.npz)
    npz.parent.mkdir(parents=True, exist_ok=True)
    hp = {**HP, "epochs": args.epochs}
    torch.manual_seed(SEED)
    if usa_gpu:
        torch.zeros(1, device="cuda")

    sistema = info_sistema()
    configuracion = dict(hp=hp, seed=SEED, image_size=IMAGE_SIZE,
                         images_per_class=args.images_per_class, prep_runs=args.prep_runs,
                         runs=args.runs, transfer_reps=args.transfer_reps,
                         targets=args.targets, npz=str(npz), amp=False,
                         cudnn_benchmark_en_gpu=True)
    print(json.dumps(sistema, indent=2, ensure_ascii=False))

    bitacora = Bitacora()
    inicio_total = time.time()
    try:
        with Monitor(salida, gpu=usa_gpu):
            preparacion, datos = medir_preparacion(args.prep_runs, args.images_per_class,
                                                   npz, bitacora)
            configuracion["huella_datos"] = huella(datos)
            configuracion["imagenes"] = {k[2:]: len(v) for k, v in datos.items() if k.startswith("y_")}
            transferencia = medir_transferencia(datos, args.transfer_reps, bitacora) if usa_gpu else []
            entrenamiento, epocas = medir_entrenamiento(datos, args.targets, args.runs, hp, bitacora)
    finally:
        if usa_gpu:
            torch.cuda.empty_cache()

    configuracion["duracion_total_s"] = round(time.time() - inicio_total, 1)
    pd.DataFrame(preparacion).to_csv(salida / "corridas_preparacion.csv", index=False)
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
