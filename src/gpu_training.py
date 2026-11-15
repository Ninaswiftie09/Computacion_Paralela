"""
gpu_training.py  --  Integrante 2: Implementación con CUDA
Proyecto 3 (CC3069): acelerar con GPU el Lab 3 ASL (Data Science).

Separación del pipeline (40 pts de la nota):
  * CPU (Integrante 1, cpu_pipeline.py): leer JPG, decodificar, Resize 64x64, split 70/15/15
    -> guarda  results/asl_prepared.npz
  * GPU (este archivo): SOLO el entrenamiento/evaluación de CNN2.

CONTRATO DE DATOS (lo que debe producir cpu_pipeline.py):
  npz con  X_train, X_val, X_test : uint8  (N, 64, 64, 3)   <- ya redimensionadas (RGB)
           y_train, y_val, y_test : int64  (N,)
           clases                  : array de 29 nombres (opcional)
  Si en cambio entrega float32 (N, 3, 64, 64) ya normalizado, también funciona.
  La normalización (x/255 - 0.5)/0.5 de uint8 se aplica aquí, en el dispositivo, igual que
  transforms.Normalize(mean=.5, std=.5) del notebook.

MODELO Y CONFIG = notebook Lab 3: CNN2 (BatchNorm+Dropout), CrossEntropy, Adam lr=1e-3,
  10 épocas, batch 64, seed 42, validación en cada época.

Modos:
  directo    : port literal del notebook (DataLoader en CPU, .to(device) por lote, .item()).
  optimizado : dataset residente en VRAM, sin sincronizaciones por lote (+ --amp opcional).

Uso:
  python gpu_training.py --check
  python gpu_training.py --modo directo     --runs 5
  python gpu_training.py --modo optimizado  --runs 5
  python gpu_training.py --modo optimizado  --runs 5 --amp     # extra (cambia precisión)
  python gpu_training.py --sintetico --runs 1 --device cpu --epochs 1   # prueba de humo
"""
import argparse, json, os, statistics, subprocess, time
import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

HP = dict(n_clases=29, epochs=10, batch_size=64, lr=1e-3, seed=42, eval_batch=512)


# ======================================================================
# MODELO: CNN2 copiada tal cual del notebook
# ======================================================================
class CNN2(nn.Module):
    def __init__(self, n_clases=29):
        super().__init__()
        self.conv = nn.Sequential(
            nn.Conv2d(3, 32, 3, padding=1), nn.BatchNorm2d(32), nn.ReLU(),
            nn.Conv2d(32, 32, 3, padding=1), nn.BatchNorm2d(32), nn.ReLU(), nn.MaxPool2d(2),
            nn.Conv2d(32, 64, 3, padding=1), nn.BatchNorm2d(64), nn.ReLU(),
            nn.Conv2d(64, 64, 3, padding=1), nn.BatchNorm2d(64), nn.ReLU(), nn.MaxPool2d(2),
            nn.Conv2d(64, 128, 3, padding=1), nn.BatchNorm2d(128), nn.ReLU(), nn.MaxPool2d(2),
        )
        self.fc = nn.Sequential(
            nn.Flatten(),
            nn.Linear(128 * 8 * 8, 256), nn.BatchNorm1d(256), nn.ReLU(),
            nn.Dropout(0.4),
            nn.Linear(256, n_clases),
        )

    def forward(self, x):
        return self.fc(self.conv(x))


# ======================================================================
# Utilidades
# ======================================================================
def _sync(dev):
    if dev.type == "cuda":
        torch.cuda.synchronize()


def normalizar(xb):
    """uint8 NHWC -> float32 NCHW normalizado a [-1, 1] (equivale a ToTensor+Normalize .5/.5).
    Si ya viene float32 se asume NCHW normalizado y no se toca."""
    if xb.dtype == torch.uint8:
        return xb.permute(0, 3, 1, 2).float().div_(255.0).sub_(0.5).div_(0.5).contiguous()
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
        z = np.load(path, allow_pickle=True)
        d = {k: z[k] for k in z.files if k.startswith(("X_", "y_"))}
    return {k: torch.from_numpy(v) for k, v in d.items()}


def verify_cuda():
    info = dict(torch=torch.__version__, cuda_disponible=torch.cuda.is_available(),
                cuda_torch=torch.version.cuda, cudnn=torch.backends.cudnn.version())
    if info["cuda_disponible"]:
        p = torch.cuda.get_device_properties(0)
        info.update(gpu=p.name, compute_capability=f"{p.major}.{p.minor}",
                    sms=p.multi_processor_count,
                    memoria_total_mb=round(p.total_memory / 2**20))
        m = CNN2().cuda()                                   # prueba real: forward+backward de CNN2
        out = m(torch.randn(8, 3, 64, 64, device="cuda"))
        out.sum().backward()
        torch.cuda.synchronize()
        info["cnn2_forward_backward_ok"] = tuple(out.shape) == (8, 29)
    return info


class SmiLogger:
    """Guarda uso de GPU/memoria cada 1 s (evidencia para el informe)."""
    def __init__(self, path, enabled=True):
        self.path, self.enabled, self.proc = path, enabled, None

    def __enter__(self):
        if not self.enabled:
            return self
        cmd = ["nvidia-smi", "--query-gpu=timestamp,utilization.gpu,utilization.memory,"
               "memory.used,memory.total,temperature.gpu,power.draw",
               "--format=csv", "-l", "1"]
        try:
            self.f = open(self.path, "w")
            self.proc = subprocess.Popen(cmd, stdout=self.f)
        except FileNotFoundError:
            print("[aviso] nvidia-smi no encontrado; no se registra uso de GPU")
        return self

    def __exit__(self, *a):
        if self.proc:
            self.proc.terminate(); self.f.close()


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
def train_once(d, hp, dev, modo, amp):
    torch.manual_seed(hp["seed"])
    cuda = dev.type == "cuda"
    if cuda:
        torch.cuda.reset_peak_memory_stats()
    crit = nn.CrossEntropyLoss()
    bs, eb = hp["batch_size"], hp["eval_batch"]

    # ---- setup: modelo (+ dataset en VRAM en modo optimizado) -------------------
    _sync(dev); t_setup = time.perf_counter()
    model = CNN2(hp["n_clases"]).to(dev)
    opt = torch.optim.Adam(model.parameters(), lr=hp["lr"])
    scaler = torch.cuda.amp.GradScaler(enabled=amp and cuda)
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

    # ---- entrenamiento (incluye validación por época, igual que el notebook) ----
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
        hist.append(dict(epoca=ep + 1, train_loss=tl, val_loss=vl, val_acc=va))
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
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="../results/asl_prepared.npz")
    ap.add_argument("--modo", default="directo", choices=["directo", "optimizado"])
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--epochs", type=int, default=HP["epochs"])
    ap.add_argument("--amp", action="store_true", help="extra: precisión mixta (no es la config del Lab 3)")
    ap.add_argument("--device", default="cuda", choices=["cuda", "cpu"])
    ap.add_argument("--sintetico", action="store_true", help="datos aleatorios, solo prueba de humo")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--out", default="../results")
    ap.add_argument("--evidence", default="../evidence")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True); os.makedirs(a.evidence, exist_ok=True)

    info = verify_cuda()
    print(json.dumps(info, indent=2, ensure_ascii=False))
    json.dump(info, open(f"{a.evidence}/cuda_check.json", "w"), indent=2)
    if a.check:
        return
    if a.device == "cuda" and not info["cuda_disponible"]:
        raise SystemExit("CUDA no disponible: revisa driver NVIDIA / modo de gráficos (laptop híbrida).")
    dev = torch.device(a.device)
    torch.backends.cudnn.benchmark = dev.type == "cuda"
    hp = {**HP, "epochs": a.epochs}

    d = cargar_datos(a.data, a.sintetico)
    print({k: tuple(v.shape) for k, v in d.items()})
    etiqueta = a.modo + ("_amp" if a.amp else "") + ("_cpu" if a.device == "cpu" else "")

    print("Calentamiento (no cuenta): 1 época sobre 512 muestras...")
    chico = {k: v[:512] for k, v in d.items()}
    train_once(chico, {**hp, "epochs": 1}, dev, a.modo, a.amp)

    res = []
    with SmiLogger(f"{a.evidence}/nvidia_smi_{etiqueta}.csv", enabled=dev.type == "cuda"):
        for r in range(a.runs):
            print(f"  Corrida {r+1}/{a.runs} [{etiqueta}]")
            res.append(train_once(d, hp, dev, a.modo, a.amp))

    summ = resumir(res)
    summ.update(hp=hp, modo=etiqueta, cuda=info, historial_ultima=res[-1]["historial"])
    json.dump(summ, open(f"{a.out}/gpu_times_{etiqueta}.json", "w"), indent=2)
    print(f"\n=== Resumen [{etiqueta}] (media ± desv, {a.runs} corridas) ===")
    for k in ("total_s", "train_s", "h2d_s", "compute_s", "epoch_mean_s", "test_s",
              "peak_mem_mb", "test_acc"):
        print(f"{k:14s} {summ[k]['mean']:.4f} ± {summ[k]['std']:.4f}")


if __name__ == "__main__":
    main()
