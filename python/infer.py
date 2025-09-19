import argparse
import time
from pathlib import Path

import sys, os  # teşhis için
import torch
import torch.nn as nn
import torch.nn.functional as F
from PIL import Image
from torchvision import models, transforms


def parse():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, help="PyTorch checkpoint (.pth/.pt)")
    ap.add_argument("--image", required=True, help="Tek resim ya da klasör yolu")
    ap.add_argument("--delay", type=float, default=0.0, help="Her tahmin sonrası bekleme (sn)")
    ap.add_argument("--limit", type=int, default=0, help="En fazla N resmi işle (0 = sınırsız)")
    ap.add_argument("--every", type=int, default=1, help="Her N resimde bir çıktı yaz")
    return ap.parse_args()


def diagnostics():
    # 'encodings' hatalarını yakalamak için hangi Python çalışıyor göster
    try:
        import encodings  # noqa
        encfile = encodings.__file__
    except Exception:
        encfile = "<bulunamadı>"

    print("PYEXE :", sys.executable, flush=True)
    print("PYVER :", sys.version, flush=True)
    print("PYHOME:", os.environ.get("PYTHONHOME"), flush=True)
    print("PYPATH:", os.environ.get("PYTHONPATH"), flush=True)
    print("ENCFILE:", encfile, flush=True)


def load_checkpoint(model_path: str):
    ckpt = torch.load(model_path, map_location="cpu")
    if isinstance(ckpt, dict) and "model_state" in ckpt and "classes" in ckpt:
        classes = ckpt["classes"]
        n = len(classes)
        m = models.resnet18(weights=None)
        m.fc = nn.Linear(m.fc.in_features, n)
        m.load_state_dict(ckpt["model_state"], strict=True)
        return m.eval(), classes
    else:
        raise RuntimeError("Checkpoint formatı beklenen değil: 'model_state' ve 'classes' anahtarları yok.")


def main():
    args = parse()
    diagnostics()

    # Model
    model, classes = load_checkpoint(args.model)

    # Transform
    tf = transforms.Compose([
        transforms.Resize((224, 224)),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406],
                             [0.229, 0.224, 0.225]),
    ])

    p = Path(args.image)
    if p.is_dir():
        files = sorted(
            list(p.glob("*.jpg")) + list(p.glob("*.jpeg")) + list(p.glob("*.png")) +
            list(p.glob("*.JPG")) + list(p.glob("*.JPEG")) + list(p.glob("*.PNG"))
        )
    else:
        files = [p]

    if not files:
        print(f"Resim bulunamadı: {p}", flush=True)
        return

    processed = 0
    for idx, f in enumerate(files, 1):
        if args.limit and processed >= args.limit:
            break
        try:
            img = Image.open(f).convert("RGB")
            x = tf(img).unsqueeze(0)
            with torch.no_grad():
                logits = model(x)
                probs = F.softmax(logits, dim=1)[0]
                i = int(probs.argmax().item())
                prob = float(probs[i])
            processed += 1

            # Detay + kısa özet
            if idx % max(1, args.every) == 0 or processed == 1:
                print(f"{f.name} -> PRED:{classes[i]} PROB:{prob:.4f}", flush=True)
                print(f"PRED: {classes[i]} ({prob:.2%})", flush=True)

            if args.delay > 0:
                time.sleep(args.delay)

        except Exception as e:
            print(f"HATA: {f} -> {e}", flush=True)

    print(f"TOPLAM: {processed} görsel işlendi.", flush=True)


if __name__ == "__main__":
    main()
