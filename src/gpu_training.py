"""
gpu_training.py: entrenamiento del modelo del Lab 3 en GPU con CUDA (PyTorch).
Proyecto 3 (CC3069): acelerar con GPU el Lab 3 ASL de Data Science.

Separación del pipeline:
  CPU (cpu_pipeline.py): leer JPG, decodificar, redimensionar a 64x64, normalizar entre 0 y 1
    y dividir en 70/15/15. Guarda results/asl_prepared.npz.
  GPU (este archivo): solo el entrenamiento y la evaluación del modelo.

Contrato de datos del NPZ:
  X_train, X_val, X_test: float32 (N, 3, 64, 64) con valores entre 0 y 1, o uint8 (N, 64, 64, 3).
  y_train, y_val, y_test: int64 (N,).
  Si llega uint8, la división entre 255 se aplica aquí, igual que en el Lab 3.

Modelo: traducción a PyTorch de build_cnn_improved de src/model_training.py (Lab 3, Keras).
  Cada bloque tiene dos Conv2D 3x3 sin sesgo con BatchNormalization y ReLU, y un MaxPooling 2x2.
  Después siguen GlobalAveragePooling, Dropout y una capa densa de 29 salidas.
  Se copian los valores por defecto de Keras: pesos Glorot uniforme, BatchNorm con eps 1e-3
  y momentum 0.99 (0.01 en PyTorch), y Adam con eps 1e-7.
  TensorFlow 2.11 en adelante no usa la GPU en Windows nativo, por eso se tradujo a PyTorch.

Modos:
  directo    : DataLoader en CPU, copia .to(device) por lote y .item() por lote.
  optimizado : conjunto completo residente en VRAM, sin sincronizaciones por lote (--amp opcional).

Uso:
  python -m src.gpu_training --check
  python -m src.gpu_training --modo directo    --runs 5
  python -m src.gpu_training --modo optimizado --runs 5
  python -m src.gpu_training --sintetico --runs 1 --device cpu --epochs 1   # prueba de humo
"""
import argparse, json, statistics, time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

PROJECT_ROOT = Path(__file__).resolve().parents[1]

# Configuraciones de CNN_IMPROVED_CONFIGS en src/model_training.py.
CNN_IMPROVED = {
    "cnn_improved_a": dict(filters=(24, 48, 96), dropout=0.35, lr=1e-3),
    "cnn_improved_b": dict(filters=(32, 64, 128), dropout=0.45, lr=5e-4),
}
# tune_keras_family del Lab 3 eligió cnn_improved_a (results/keras_referencia/ajuste_cnn_improved.csv).
CONFIG = "cnn_improved_a"
HP = dict(n_clases=29, epochs=10, batch_size=64, lr=CNN_IMPROVED[CONFIG]["lr"], seed=42,
          eval_batch=512, config=CONFIG)


# ======================================================================
# MODELO: build_cnn_improved del Lab 3 traducido a PyTorch
# ======================================================================
class CNNMejorada(nn.Module):
    def __init__(self, filters=(32, 64, 128), dropout=0.45, n_clases=29):
        super().__init__()
        capas, entrada = [], 3
        for salida in filters:
            capas += [
                nn.Conv2d(entrada, salida, 3, padding=1, bias=False),
                nn.BatchNorm2d(salida, eps=1e-3, momentum=0.01), nn.ReLU(),
                nn.Conv2d(salida, salida, 3, padding=1, bias=False),
                nn.BatchNorm2d(salida, eps=1e-3, momentum=0.01), nn.ReLU(),
                nn.MaxPool2d(2),
            ]
            entrada = salida
        self.conv = nn.Sequential(*capas)
        self.fc = nn.Sequential(
            nn.AdaptiveAvgPool2d(1), nn.Flatten(),
            nn.Dropout(dropout),
            nn.Linear(entrada, n_clases),
        )
        for modulo in self.modules():
            if isinstance(modulo, (nn.Conv2d, nn.Linear)):
                nn.init.xavier_uniform_(modulo.weight)
                if modulo.bias is not None:
                    nn.init.zeros_(modulo.bias)

    def forward(self, x):
        return self.fc(self.conv(x))


def crear_modelo(hp):
    config = CNN_IMPROVED[hp["config"]]
    return CNNMejorada(config["filters"], config["dropout"], hp["n_clases"])


# ======================================================================
# Utilidades
# ======================================================================
def _sync(dev):
    if dev.type == "cuda":
        torch.cuda.synchronize()


def normalizar(xb):
    """uint8 NHWC -> float32 NCHW entre 0 y 1, igual que la división entre 255 del Lab 3.
    Si ya viene float32 se asume NCHW normalizado y no se toca."""
    if xb.dtype == torch.uint8:
        return xb.permute(0, 3, 1, 2).float().div_(255.0).contiguous()
    return xb


def cargar_datos(path, sintetico=False, n_sint=2048):
    if sintetico:
        g = np.random.default_rng(0)
        mk = lambda n: (g.integers(0, 256, (n, 64, 64, 3), dtype=np.uint8),
                        g.integers(0, 29, n).astype(np.int64))
        d = {}
        for k, n in (("train", n_sint), ("val", n_sint // 4), ("test", n_sint // 4)):
            d[f"X_{k}"], d[f"y_{k}"] = mk(n)
    else:
        with np.load(path, allow_pickle=True) as z:
            d = {k: z[k] for k in z.files if k.startswith(("X_", "y_"))}
    return {k: torch.from_numpy(v) for k, v in d.items()}


def verify_cuda(hp=HP):
    info = dict(torch=torch.__version__, cuda_disponible=torch.cuda.is_available(),
                cuda_torch=torch.version.cuda, cudnn=torch.backends.cudnn.version())
    if info["cuda_disponible"]:
        p = torch.cuda.get_device_properties(0)
        info.update(gpu=p.name, compute_capability=f"{p.major}.{p.minor}",
                    sms=p.multi_processor_count,
                    memoria_total_mb=round(p.total_memory / 2**20))
        m = crear_modelo(hp).cuda()                       # prueba real: forward y backward
        out = m(torch.rand(8, 3, 64, 64, device="cuda"))
        out.sum().backward()
        torch.cuda.synchronize()
        info["modelo_forward_backward_ok"] = tuple(out.shape) == (8, hp["n_clases"])
    return info


# ======================================================================
# Evaluación
# ======================================================================
@torch.no_grad()
def eval_residente(model, X, y, crit, bs, amp):
    model.eval()
    loss = torch.zeros((), device=X.device); ok = torch.zeros((), device=X.device)
    for s in range(0, len(X), bs):
        xb, yb = normalizar(X[s:s + bs]), y[s:s + bs]
        with torch.autocast("cuda", enabled=amp):
            out = model(xb)
        loss += crit(out.float(), yb) * len(xb)
        ok += (out.argmax(1) == yb).sum()
    return float(loss) / len(X), float(ok) / len(X)


@torch.no_grad()
def eval_directo(model, loader, crit, dev):
    model.eval()
    loss, ok, n = 0.0, 0, 0
    for xb, yb in loader:
        xb, yb = normalizar(xb.to(dev)), yb.to(dev)
        out = model(xb)
        loss += crit(out, yb).item() * len(xb)
        ok += (out.argmax(1) == yb).sum().item(); n += len(xb)
    return loss / n, ok / n


# ======================================================================
# Un entrenamiento completo
# ======================================================================
def train_once(d, hp, dev, modo, amp, modelo=None):
    torch.manual_seed(hp["seed"])
    cuda = dev.type == "cuda"
    if cuda:
        torch.cuda.reset_peak_memory_stats()
    crit = nn.CrossEntropyLoss()
    bs, eb = hp["batch_size"], hp["eval_batch"]

    # ---- setup: modelo (+ dataset en VRAM en modo optimizado) -------------------
    _sync(dev); t_setup = time.perf_counter()
    model = (modelo or crear_modelo)(hp).to(dev)
    opt = torch.optim.Adam(model.parameters(), lr=hp["lr"], eps=1e-7)
    scaler = torch.amp.GradScaler("cuda", enabled=amp and cuda)
    h2d = 0.0
    if modo == "optimizado":
        Xtr, ytr = d["X_train"].to(dev), d["y_train"].to(dev)
        Xva, yva = d["X_val"].to(dev), d["y_val"].to(dev)
        Xte, yte = d["X_test"].to(dev), d["y_test"].to(dev)
        gen = torch.Generator(device=dev).manual_seed(hp["seed"])
    else:
        tr_loader = DataLoader(TensorDataset(d["X_train"], d["y_train"]), batch_size=bs, shuffle=True)
        va_loader = DataLoader(TensorDataset(d["X_val"], d["y_val"]), batch_size=bs)
        te_loader = DataLoader(TensorDataset(d["X_test"], d["y_test"]), batch_size=bs)
    _sync(dev); setup_s = time.perf_counter() - t_setup
    if modo == "optimizado":
        h2d = setup_s                                   # aquí el setup es casi solo la copia

    # ---- entrenamiento (incluye validación por época, igual que el Lab 3) -------
    ev, epoch_s, hist = [], [], []
    _sync(dev); t0 = time.perf_counter()
    for ep in range(hp["epochs"]):
        te0 = time.perf_counter()
        model.train()
        acum = torch.zeros((), device=dev)
        if modo == "optimizado":
            perm = torch.randperm(len(Xtr), device=dev, generator=gen)
            for s in range(0, len(Xtr), bs):
                idx = perm[s:s + bs]
                xb, yb = normalizar(Xtr[idx]), ytr[idx]
                with torch.autocast("cuda", enabled=amp and cuda):
                    loss = crit(model(xb), yb)
                opt.zero_grad(set_to_none=True)
                scaler.scale(loss).backward(); scaler.step(opt); scaler.update()
                acum += loss.detach() * len(xb)         # sin .item(): no bloquea la GPU
            tl = float(acum) / len(Xtr)
            vl, va = eval_residente(model, Xva, yva, crit, eb, amp and cuda)
        else:
            tl = 0.0
            for xb, yb in tr_loader:
                if cuda:
                    e0, e1 = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
                    e0.record(); xb, yb = xb.to(dev), yb.to(dev); e1.record(); ev.append((e0, e1))
                else:
                    xb, yb = xb.to(dev), yb.to(dev)
                xb = normalizar(xb)
                opt.zero_grad()
                loss = crit(model(xb), yb)
                loss.backward(); opt.step()
                tl += loss.item() * len(xb)             # port literal: sincroniza por lote
            tl /= len(tr_loader.dataset)
            vl, va = eval_directo(model, va_loader, crit, dev)
        _sync(dev); epoch_s.append(time.perf_counter() - te0)
        hist.append(dict(epoca=ep + 1, train_loss=tl, val_loss=vl, val_acc=va, segundos=epoch_s[-1]))
        print(f"    ép {ep+1:02d}/{hp['epochs']} train_loss={tl:.4f} val_loss={vl:.4f} "
              f"val_acc={va:.4f}  ({epoch_s[-1]:.2f}s)")
    _sync(dev); train_s = time.perf_counter() - t0
    if ev:
        torch.cuda.synchronize()
        h2d = sum(a.elapsed_time(b) for a, b in ev) / 1000.0

    # ---- test (fase aparte) -----------------------------------------------------
    _sync(dev); t1 = time.perf_counter()
    if modo == "optimizado":
        _, test_acc = eval_residente(model, Xte, yte, crit, eb, amp and cuda)
    else:
        _, test_acc = eval_directo(model, te_loader, crit, dev)
    _sync(dev); test_s = time.perf_counter() - t1

    return dict(setup_s=setup_s, h2d_s=h2d, train_s=train_s, test_s=test_s,
                total_s=setup_s + train_s + test_s,
                compute_s=train_s - (h2d if modo == "directo" else 0.0),
                epoch_mean_s=statistics.mean(epoch_s),
                peak_mem_mb=(torch.cuda.max_memory_allocated() / 2**20) if cuda else 0.0,
                val_acc_final=hist[-1]["val_acc"], test_acc=test_acc, historial=hist)


def resumir(res):
    keys = ("setup_s", "h2d_s", "train_s", "test_s", "total_s", "compute_s",
            "epoch_mean_s", "peak_mem_mb", "val_acc_final", "test_acc")
    return {k: dict(mean=statistics.mean(r[k] for r in res),
                    std=statistics.stdev(r[k] for r in res) if len(res) > 1 else 0.0,
                    runs=[r[k] for r in res]) for k in keys}


# ======================================================================
def main():
    from .monitor import Monitor

    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=str(PROJECT_ROOT / "results" / "asl_prepared.npz"))
    ap.add_argument("--modo", default="directo", choices=["directo", "optimizado"])
    ap.add_argument("--config", default=CONFIG, choices=list(CNN_IMPROVED))
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--epochs", type=int, default=HP["epochs"])
    ap.add_argument("--amp", action="store_true", help="extra: precisión mixta (no es la config del Lab 3)")
    ap.add_argument("--device", default="cuda", choices=["cuda", "cpu"])
    ap.add_argument("--sintetico", action="store_true", help="datos aleatorios, solo prueba de humo")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--out", default=str(PROJECT_ROOT / "results"))
    ap.add_argument("--evidence", default=str(PROJECT_ROOT / "evidence"))
    a = ap.parse_args()
    out, evidence = Path(a.out), Path(a.evidence)
    out.mkdir(parents=True, exist_ok=True); evidence.mkdir(parents=True, exist_ok=True)
    hp = {**HP, "epochs": a.epochs, "config": a.config, "lr": CNN_IMPROVED[a.config]["lr"]}

    info = verify_cuda(hp)
    print(json.dumps(info, indent=2, ensure_ascii=False))
    (evidence / "cuda_check.json").write_text(json.dumps(info, indent=2), encoding="utf-8")
    if a.check:
        return
    if a.device == "cuda" and not info["cuda_disponible"]:
        raise SystemExit("CUDA no disponible: revisa driver NVIDIA / modo de gráficos (laptop híbrida).")
    dev = torch.device(a.device)
    torch.backends.cudnn.benchmark = dev.type == "cuda"

    d = cargar_datos(a.data, a.sintetico)
    print({k: tuple(v.shape) for k, v in d.items()})
    etiqueta = a.modo + ("_amp" if a.amp else "") + ("_cpu" if a.device == "cpu" else "")

    print("Calentamiento (no cuenta): 1 época sobre 512 muestras...")
    chico = {k: v[:512] for k, v in d.items()}
    train_once(chico, {**hp, "epochs": 1}, dev, a.modo, a.amp)

    res = []
    with Monitor(evidence, gpu=dev.type == "cuda", prefijo=f"monitor_{etiqueta}"):
        for r in range(a.runs):
            print(f"  Corrida {r+1}/{a.runs} [{etiqueta}]")
            res.append(train_once(d, hp, dev, a.modo, a.amp))

    summ = resumir(res)
    summ.update(hp=hp, modo=etiqueta, cuda=info, historial_ultima=res[-1]["historial"])
    (out / f"gpu_times_{etiqueta}.json").write_text(json.dumps(summ, indent=2), encoding="utf-8")
    print(f"\n=== Resumen [{etiqueta}] (media ± desv, {a.runs} corridas) ===")
    for k in ("total_s", "train_s", "h2d_s", "compute_s", "epoch_mean_s", "test_s",
              "peak_mem_mb", "test_acc"):
        print(f"{k:14s} {summ[k]['mean']:.4f} ± {summ[k]['std']:.4f}")


if __name__ == "__main__":
    main()
