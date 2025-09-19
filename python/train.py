import argparse
import sys
from pathlib import Path

import os  # teşhis için
import torch
import torch.nn as nn
from torch.optim import Adam
from torch.utils.data import DataLoader, random_split, Subset
from torchvision import datasets, transforms, models
from tqdm import tqdm


def log(*a): print(*a, flush=True)


def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, help="Dataset kökü (altında class klasörleri)")
    ap.add_argument("--out",  required=True, help="Çıktı klasörü (model_out gibi)")
    ap.add_argument("--epochs", type=int, default=3)
    ap.add_argument("--batch",  type=int, default=32)
    ap.add_argument("--lr",     type=float, default=1e-3)
    ap.add_argument("--pretrained", type=int, default=1, help="ResNet18 ön-eğitimli (1) / sıfırdan (0)")
    ap.add_argument("--workers", type=int, default=0, help="Windows için 0 güvenli")
    return ap.parse_args()


def diagnostics():
    try:
        import encodings  # noqa
        encfile = encodings.__file__
    except Exception:
        encfile = "<bulunamadı>"

    log("PYEXE :", sys.executable)
    log("PYVER :", sys.version)
    log("PYHOME:", os.environ.get("PYTHONHOME"))
    log("PYPATH:", os.environ.get("PYTHONPATH"))
    log("ENCFILE:", encfile)


def main():
    args = parse_args()
    diagnostics()

    data_root = Path(args.data)
    out_dir   = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    tf_train = transforms.Compose([
        transforms.Resize((224, 224)),
        transforms.RandomHorizontalFlip(),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406],
                             [0.229, 0.224, 0.225]),
    ])
    tf_val = transforms.Compose([
        transforms.Resize((224, 224)),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406],
                             [0.229, 0.224, 0.225]),
    ])

    # Ayrı ImageFolder örnekleri
    full_train = datasets.ImageFolder(str(data_root), transform=tf_train)
    if len(full_train.classes) < 2:
        log("HATA: En az 2 sınıf gerekli. Bulunan:", full_train.classes)
        sys.exit(2)
    log("Sınıflar:", full_train.classes)

    n = len(full_train)
    n_val = max(1, int(n * 0.15))
    n_train = max(1, n - n_val)
    train_idx_subset, val_idx_subset = random_split(range(n), [n_train, n_val])

    full_val = datasets.ImageFolder(str(data_root), transform=tf_val)
    train_ds = Subset(full_train, train_idx_subset.indices)
    val_ds   = Subset(full_val,   val_idx_subset.indices)

    device = "cuda" if torch.cuda.is_available() else "cpu"
    log("Cihaz:", device)

    # torchvision sürüm uyumu: ön-eğitimli ağırlık seçimi
    weights = None
    if args.pretrained:
        try:
            weights = models.ResNet18_Weights.DEFAULT
        except AttributeError:
            weights = True  # eski sürümlerde bool kabul edilir

    model = models.resnet18(weights=weights)
    if not hasattr(model, "fc") or not hasattr(model.fc, "in_features"):
        log("HATA: resnet18 mimarisi beklenenden farklı.")
        sys.exit(3)
    model.fc = nn.Linear(model.fc.in_features, len(full_train.classes))
    model.to(device)

    opt   = Adam(model.parameters(), lr=args.lr)
    lossf = nn.CrossEntropyLoss()

    pin = (device == "cuda")
    train_dl = DataLoader(train_ds, batch_size=args.batch, shuffle=True,
                          num_workers=args.workers, pin_memory=pin)
    val_dl   = DataLoader(val_ds,   batch_size=args.batch, shuffle=False,
                          num_workers=args.workers, pin_memory=pin)

    best = 0.0
    for ep in range(1, args.epochs + 1):
        model.train()
        pbar = tqdm(train_dl, desc=f"Epoch {ep}/{args.epochs}", file=sys.stdout, leave=False)
        for x, y in pbar:
            x, y = x.to(device), y.to(device)
            opt.zero_grad()
            out = model(x)
            loss = lossf(out, y)
            loss.backward()
            opt.step()

        model.eval(); corr = tot = 0
        with torch.no_grad():
            for x, y in val_dl:
                x, y = x.to(device), y.to(device)
                pred = model(x).argmax(1)
                corr += (pred == y).sum().item()
                tot  += y.numel()
        acc = corr / tot if tot else 0.0
        log(f"VAL_ACC:{acc:.4f}")

        if acc > best:
            best = acc
            torch.save(
                {"model_state": model.state_dict(), "classes": full_train.classes},
                out_dir / "model_best.pth"
            )
    log("DONE")


if __name__ == "__main__":
    main()
