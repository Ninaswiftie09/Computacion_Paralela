"""Registro de los kernels CUDA que lanza el entrenamiento, con su configuración de grid y bloque.

Usa torch.profiler (CUPTI) sobre varios pasos de entrenamiento del modo optimizado, con el
modelo estándar (cuDNN y cuBLAS) y con la capa densa de Numba. Para cada kernel guarda cuántas
veces se lanzó, su tiempo, la forma del grid y del bloque, los registros por hilo, la memoria
compartida y la ocupación estimada. También verifica los kernels de Numba contra PyTorch.

Uso:
  python -m src.gpu_profile --npz C:/Users/usuario/datasets/p3_work/asl_prepared.npz
"""
from __future__ import annotations

import argparse
import json
import tempfile
from pathlib import Path

import pandas as pd
import torch
import torch.nn as nn

from .gpu_training import HP, PROJECT_ROOT, cargar_datos, crear_modelo, normalizar

CATEGORIAS = (
    ("Kernel propio de Numba", ("matmul_tiles", "sumar_sesgo", "sumar_columnas")),
    ("Normalización por lotes", ("bn_fw", "bn_bw", "batch_norm", "batchnorm")),
    ("Cambio de formato NCHW y NHWC", ("nchwtonhwc", "nhwctonchw")),
    ("Convolución", ("conv", "implicit_gemm", "xmma", "winograd", "fft", "cudnn", "dgrad", "wgrad", "fprop")),
    ("Pooling", ("pool",)),
    ("Multiplicación de matrices", ("gemm", "gemv", "sgemm", "cutlass")),
    ("Optimizador Adam", ("adam", "multi_tensor", "foreach")),
    ("Pérdida y softmax", ("softmax", "nll", "cross_entropy", "log_")),
    ("Reducciones", ("reduce",)),
    ("Operaciones elemento a elemento", ("elementwise", "vectorized", "unrolled", "threshold", "relu", "fill")),
)


def categoria(nombre: str) -> str:
    bajo = nombre.lower()
    for etiqueta, claves in CATEGORIAS:
        if any(clave in bajo for clave in claves):
            return etiqueta
    return "Otros"


def kernels_de_traza(ruta: Path, pasos: int) -> pd.DataFrame:
    eventos = json.loads(ruta.read_text(encoding="utf-8"))["traceEvents"]
    filas = []
    for evento in eventos:
        if evento.get("cat") != "kernel":
            continue
        args = evento.get("args", {})
        filas.append(dict(
            kernel=evento["name"],
            categoria=categoria(evento["name"]),
            duracion_us=float(evento.get("dur", 0)),
            grid=str(args.get("grid")),
            bloque=str(args.get("block")),
            registros_por_hilo=args.get("registers per thread"),
            memoria_compartida_b=args.get("shared memory"),
            # CUPTI reporta 0 cuando no puede estimar la ocupación del kernel.
            ocupacion_pct=args.get("est. achieved occupancy %") or None,
            hilos=_producto(args.get("grid")) * _producto(args.get("block")),
        ))
    tabla = pd.DataFrame(filas)
    resumen = (tabla.groupby(["categoria", "kernel", "grid", "bloque"], as_index=False)
               .agg(lanzamientos=("duracion_us", "size"), total_us=("duracion_us", "sum"),
                    media_us=("duracion_us", "mean"), hilos=("hilos", "first"),
                    registros_por_hilo=("registros_por_hilo", "first"),
                    memoria_compartida_b=("memoria_compartida_b", "first"),
                    ocupacion_pct=("ocupacion_pct", "first")))
    resumen["lanzamientos_por_paso"] = resumen["lanzamientos"] / pasos
    resumen["porcentaje_tiempo"] = 100 * resumen["total_us"] / resumen["total_us"].sum()
    return resumen.sort_values("total_us", ascending=False)


def _producto(valor) -> int:
    if not isinstance(valor, (list, tuple)):
        return 0
    resultado = 1
    for v in valor:
        resultado *= int(v)
    return resultado


def perfilar(fabrica, datos: dict, pasos: int, salida_traza: Path) -> None:
    dispositivo = torch.device("cuda")
    torch.manual_seed(HP["seed"])
    torch.backends.cudnn.benchmark = True
    modelo = fabrica(HP).to(dispositivo)
    optimizador = torch.optim.Adam(modelo.parameters(), lr=HP["lr"], eps=1e-7)
    criterio = nn.CrossEntropyLoss()
    bs = HP["batch_size"]
    X = datos["X_train"][: (pasos + 5) * bs].to(dispositivo)
    y = datos["y_train"][: (pasos + 5) * bs].to(dispositivo)
    modelo.train()

    def paso(i: int) -> None:
        xb, yb = normalizar(X[i * bs:(i + 1) * bs]), y[i * bs:(i + 1) * bs]
        perdida = criterio(modelo(xb), yb)
        optimizador.zero_grad(set_to_none=True)
        perdida.backward()
        optimizador.step()

    for i in range(5):
        paso(i)
    torch.cuda.synchronize()
    actividades = [torch.profiler.ProfilerActivity.CPU, torch.profiler.ProfilerActivity.CUDA]
    with torch.profiler.profile(activities=actividades) as perfil:
        for i in range(5, 5 + pasos):
            paso(i)
        torch.cuda.synchronize()
    perfil.export_chrome_trace(str(salida_traza))


def main() -> None:
    parser = argparse.ArgumentParser(description="Kernels CUDA del entrenamiento")
    parser.add_argument("--npz", default=str(PROJECT_ROOT / "results" / "asl_prepared.npz"))
    parser.add_argument("--out", default=str(PROJECT_ROOT / "results" / "perfil_gpu"))
    parser.add_argument("--pasos", type=int, default=20)
    args = parser.parse_args()
    if not torch.cuda.is_available():
        raise SystemExit("CUDA no está disponible.")
    salida = Path(args.out)
    salida.mkdir(parents=True, exist_ok=True)

    from .numba_dense import crear_modelo_numba, verificar

    verificacion = verificar()
    (salida / "verificacion_numba.json").write_text(
        json.dumps(verificacion, indent=2, ensure_ascii=False), encoding="utf-8")
    print("Diferencia máxima contra PyTorch:", verificacion["diferencia_maxima"])

    datos = cargar_datos(args.npz)
    resumen = {}
    with tempfile.TemporaryDirectory() as temporal:
        for nombre, fabrica in (("estandar", crear_modelo), ("numba", crear_modelo_numba)):
            traza = Path(temporal) / f"traza_{nombre}.json"
            perfilar(fabrica, datos, args.pasos, traza)
            tabla = kernels_de_traza(traza, args.pasos)
            tabla.to_csv(salida / f"kernels_{nombre}.csv", index=False)
            por_categoria = (tabla.groupby("categoria")[["total_us", "lanzamientos"]].sum()
                             .sort_values("total_us", ascending=False))
            por_categoria["porcentaje_tiempo"] = 100 * por_categoria["total_us"] / por_categoria["total_us"].sum()
            por_categoria["lanzamientos_por_paso"] = por_categoria["lanzamientos"] / args.pasos
            por_categoria.to_csv(salida / f"categorias_{nombre}.csv")
            resumen[nombre] = dict(
                kernels_distintos=int(tabla["kernel"].nunique()),
                lanzamientos_por_paso=float(tabla["lanzamientos"].sum() / args.pasos),
                tiempo_gpu_por_paso_ms=float(tabla["total_us"].sum() / args.pasos / 1000),
            )
            print(f"\n[{nombre}] {resumen[nombre]}")
            print(por_categoria.round(2).to_string())
    propiedades = torch.cuda.get_device_properties(0)
    resumen["gpu"] = dict(nombre=propiedades.name, sms=propiedades.multi_processor_count,
                          hilos_max_por_sm=getattr(propiedades, "max_threads_per_multi_processor", None),
                          warp=32, pasos_perfilados=args.pasos, lote=HP["batch_size"])
    (salida / "resumen.json").write_text(json.dumps(resumen, indent=2, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()
