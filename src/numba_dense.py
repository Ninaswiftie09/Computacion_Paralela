"""Capa densa del modelo con kernels CUDA propios escritos en Python con Numba.

Reemplaza la última capa de CNNMejorada (96 entradas, 29 salidas) por kernels escritos a mano,
para mostrar de forma explícita cómo se reparte el trabajo entre hilos, bloques y grid.
El resto del modelo sigue usando cuDNN y cuBLAS a través de PyTorch.

Reparto del trabajo:
  matmul_tiles: cada hilo calcula un elemento de C = op(A) op(B). Los hilos se agrupan en
    bloques de 16x16 y el grid cubre toda la matriz de salida con ceil(N/16) x ceil(M/16)
    bloques. Cada bloque recorre la dimensión K en mosaicos de 16 que copia a memoria
    compartida, así cada valor de A y B se lee de la memoria global una vez por bloque.
  sumar_sesgo: un hilo por elemento de la salida, bloques de 16x16.
  sumar_columnas: un bloque de 256 hilos por columna. Cada hilo acumula varias filas y
    luego el bloque hace una reducción en árbol en memoria compartida.

Forward:  Y = X W^T + b        (N x K) (K x M)
Backward: dX = dY W            (N x M) (M x K)
          dW = dY^T X          (M x N) (N x K)
          db = suma de dY por columnas

Los tensores de PyTorch se pasan a Numba sin copiar mediante __cuda_array_interface__, y los
kernels se lanzan en el mismo stream que usa PyTorch para respetar el orden de ejecución.

Uso:
  python -m src.numba_dense      # verifica contra torch.nn.functional.linear
"""
from __future__ import annotations

import math

import torch
import torch.nn as nn
import torch.nn.functional as F
from numba import cuda, float32

TILE = 16
HILOS_REDUCCION = 256


@cuda.jit
def matmul_tiles(A, B, C, trans_a, trans_b):
    """C = op(A) @ op(B), donde op transpone si la bandera es verdadera."""
    sA = cuda.shared.array((TILE, TILE), float32)
    sB = cuda.shared.array((TILE, TILE), float32)
    tx = cuda.threadIdx.x
    ty = cuda.threadIdx.y
    fila = cuda.blockIdx.y * TILE + ty
    col = cuda.blockIdx.x * TILE + tx
    M = C.shape[0]
    N = C.shape[1]
    K = A.shape[0] if trans_a else A.shape[1]
    acumulado = float32(0.0)
    for mosaico in range((K + TILE - 1) // TILE):
        k = mosaico * TILE + tx
        if fila < M and k < K:
            sA[ty, tx] = A[k, fila] if trans_a else A[fila, k]
        else:
            sA[ty, tx] = 0.0
        k = mosaico * TILE + ty
        if k < K and col < N:
            sB[ty, tx] = B[col, k] if trans_b else B[k, col]
        else:
            sB[ty, tx] = 0.0
        cuda.syncthreads()
        for i in range(TILE):
            acumulado += sA[ty, i] * sB[i, tx]
        cuda.syncthreads()
    if fila < M and col < N:
        C[fila, col] = acumulado


@cuda.jit
def sumar_sesgo(Y, b):
    col, fila = cuda.grid(2)
    if fila < Y.shape[0] and col < Y.shape[1]:
        Y[fila, col] += b[col]


@cuda.jit
def sumar_columnas(G, salida):
    parcial = cuda.shared.array(HILOS_REDUCCION, float32)
    col = cuda.blockIdx.x
    hilo = cuda.threadIdx.x
    acumulado = float32(0.0)
    fila = hilo
    while fila < G.shape[0]:
        acumulado += G[fila, col]
        fila += cuda.blockDim.x
    parcial[hilo] = acumulado
    cuda.syncthreads()
    paso = cuda.blockDim.x // 2
    while paso > 0:
        if hilo < paso:
            parcial[hilo] += parcial[hilo + paso]
        cuda.syncthreads()
        paso //= 2
    if hilo == 0:
        salida[col] = parcial[0]


def _stream():
    return cuda.external_stream(torch.cuda.current_stream().cuda_stream)


def _vista(tensor: torch.Tensor):
    return cuda.as_cuda_array(tensor.detach())


def configuracion_matmul(filas: int, columnas: int) -> tuple[tuple[int, int], tuple[int, int]]:
    return (math.ceil(columnas / TILE), math.ceil(filas / TILE)), (TILE, TILE)


def matmul(A: torch.Tensor, B: torch.Tensor, trans_a: bool, trans_b: bool) -> torch.Tensor:
    filas = A.shape[1] if trans_a else A.shape[0]
    columnas = B.shape[0] if trans_b else B.shape[1]
    C = torch.empty((filas, columnas), device=A.device, dtype=torch.float32)
    grid, bloque = configuracion_matmul(filas, columnas)
    matmul_tiles[grid, bloque, _stream()](_vista(A), _vista(B), _vista(C), trans_a, trans_b)
    return C


class _DensaNumba(torch.autograd.Function):
    @staticmethod
    def forward(ctx, x, peso, sesgo):
        x = x.contiguous()
        ctx.save_for_backward(x, peso)
        y = matmul(x, peso, False, True)
        grid, bloque = configuracion_matmul(*y.shape)
        sumar_sesgo[grid, bloque, _stream()](_vista(y), _vista(sesgo))
        return y

    @staticmethod
    def backward(ctx, grad_y):
        x, peso = ctx.saved_tensors
        grad_y = grad_y.contiguous()
        grad_x = matmul(grad_y, peso, False, False)
        grad_peso = matmul(grad_y, x, True, False)
        grad_sesgo = torch.empty(peso.shape[0], device=x.device, dtype=torch.float32)
        sumar_columnas[peso.shape[0], HILOS_REDUCCION, _stream()](_vista(grad_y), _vista(grad_sesgo))
        return grad_x, grad_peso, grad_sesgo


class DensaNumba(nn.Module):
    """Equivale a nn.Linear. En GPU usa los kernels de Numba y en CPU usa PyTorch."""

    def __init__(self, entradas: int, salidas: int):
        super().__init__()
        self.weight = nn.Parameter(torch.empty(salidas, entradas))
        self.bias = nn.Parameter(torch.zeros(salidas))
        nn.init.xavier_uniform_(self.weight)

    def forward(self, x):
        if x.is_cuda:
            return _DensaNumba.apply(x, self.weight.contiguous(), self.bias)
        return F.linear(x, self.weight, self.bias)


def crear_modelo_numba(hp):
    """CNNMejorada del Lab 3 con la capa densa final hecha con kernels de Numba."""
    from .gpu_training import crear_modelo

    modelo = crear_modelo(hp)
    lineal = modelo.fc[-1]
    densa = DensaNumba(lineal.in_features, lineal.out_features)
    with torch.no_grad():
        densa.weight.copy_(lineal.weight)
        densa.bias.copy_(lineal.bias)
    modelo.fc[-1] = densa
    return modelo


def verificar(lote: int = 64, entradas: int = 96, salidas: int = 29) -> dict:
    """Compara forward y backward de los kernels contra PyTorch con datos aleatorios."""
    torch.manual_seed(0)
    dispositivo = torch.device("cuda")
    x = torch.randn(lote, entradas, device=dispositivo, requires_grad=True)
    peso = torch.randn(salidas, entradas, device=dispositivo, requires_grad=True)
    sesgo = torch.randn(salidas, device=dispositivo, requires_grad=True)
    gy = torch.randn(lote, salidas, device=dispositivo)

    y = _DensaNumba.apply(x, peso, sesgo)
    y.backward(gy)
    propios = [y.detach().clone(), x.grad.clone(), peso.grad.clone(), sesgo.grad.clone()]
    for t in (x, peso, sesgo):
        t.grad = None
    y_ref = F.linear(x, peso, sesgo)
    y_ref.backward(gy)
    referencia = [y_ref.detach(), x.grad, peso.grad, sesgo.grad]
    nombres = ("salida", "grad_x", "grad_peso", "grad_sesgo")
    diferencias = {n: float((a - b).abs().max()) for n, a, b in zip(nombres, propios, referencia)}
    lanzamientos = {
        "forward matmul_tiles": configuracion_matmul(lote, salidas),
        "forward sumar_sesgo": configuracion_matmul(lote, salidas),
        "backward matmul_tiles grad_x": configuracion_matmul(lote, entradas),
        "backward matmul_tiles grad_peso": configuracion_matmul(salidas, entradas),
        "backward sumar_columnas grad_sesgo": ((salidas,), (HILOS_REDUCCION,)),
    }
    return dict(diferencia_maxima=diferencias, lanzamientos=lanzamientos)


if __name__ == "__main__":
    import json

    resultado = verificar()
    print(json.dumps(resultado, indent=2, ensure_ascii=False))
    assert all(v < 1e-4 for v in resultado["diferencia_maxima"].values()), "Los kernels no coinciden"
    print("Los kernels de Numba coinciden con PyTorch.")
