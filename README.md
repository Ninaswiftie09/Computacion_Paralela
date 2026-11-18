# Proyecto 3: Mejorando algoritmos con CUDA

Computación Paralela y Distribuida, UVG, Sección 20.

| Integrante | Carné |
|---|---|
| Roberto Barreda | 23354 |
| Nina Najera | 231088 |
| José Antón | 221041 |

Se aceleró el clasificador de señas ASL del Laboratorio 3 de Data Science. La preparación de imágenes corre en el CPU con varios procesos y el entrenamiento de la CNN corre en la GPU con CUDA (PyTorch, cuDNN y kernels propios con Numba). El informe está en [docs/informe/informe.pdf](docs/informe/informe.pdf) y el recorrido del código en [notebooks/Proyecto3_CUDA.ipynb](notebooks/Proyecto3_CUDA.ipynb).

## Resultados

| Fase | CPU | GPU optimizado | Speedup |
|---|---:|---:|---:|
| Entrenamiento, 10 épocas | 176.00 s | 11.53 s | 15.27x |
| Pipeline completo | 238.71 s | 73.02 s | 3.27x |
| Pipeline con preparación en 8 procesos | 238.71 s | 63.83 s | 3.74x |

## Ejecución

Instalar las dependencias. En Windows, PyTorch con CUDA se instala desde `https://download.pytorch.org/whl/cu130`.

```bash
pip install -r requirements.txt
```

Descargar el dataset en `data/raw`.

```bash
python -m src.download_dataset
```

Ejecutar el benchmark completo y generar tablas y figuras en `results/benchmark`. Conviene guardar el NPZ fuera de carpetas sincronizadas como OneDrive.

```bash
python -m src.benchmark --npz RUTA/asl_prepared.npz
```

```bash
python -m src.benchmark_report
```

Registrar los kernels de la GPU y verificar los kernels de Numba.

```bash
python -m src.gpu_profile --npz RUTA/asl_prepared.npz
```

Regenerar las tablas del informe.

```bash
python docs/informe/generar_tablas.py
```
