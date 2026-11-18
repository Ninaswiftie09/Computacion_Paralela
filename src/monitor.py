"""Monitor de recursos que registra cada segundo el uso por núcleo del CPU y el estado de la GPU.

Corre en un hilo daemon que se detiene al salir del bloque with, incluso con errores.
nvidia-smi se llama una vez por muestra con tiempo límite, así no queda ningún proceso hijo
vivo si el programa se interrumpe.
"""
from __future__ import annotations

import csv
import subprocess
import threading
import time
from pathlib import Path

import psutil

CONSULTA_GPU = (
    "utilization.gpu,utilization.memory,memory.used,memory.total,"
    "temperature.gpu,power.draw,clocks.sm"
)


class Monitor:
    def __init__(self, carpeta: Path, intervalo: float = 1.0, gpu: bool = True, prefijo: str = "monitor"):
        self.cpu_path = Path(carpeta) / f"{prefijo}_cpu.csv"
        self.gpu_path = Path(carpeta) / f"{prefijo}_gpu.csv"
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
