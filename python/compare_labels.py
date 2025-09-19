# compare_labels.py  — YOLO txt etiketlerini IoU ile karşılaştırma
import argparse, csv
from pathlib import Path
from typing import List, Tuple, Dict

BBox = Tuple[int, float, float, float, float]  # (class_id, cx, cy, w, h)

def read_yolo_file(path: Path) -> List[BBox]:
    if not path.exists(): return []
    out = []
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            t = line.strip()
            if not t: continue
            p = t.split()
            cls = int(float(p[0])); cx,cy,w,h = map(float, p[1:5])
            out.append((cls,cx,cy,w,h))
    return out

def yolo_to_xyxy(bb: BBox):
    cls,cx,cy,w,h = bb
    x1, y1 = cx - w/2, cy - h/2
    x2, y2 = cx + w/2, cy + h/2
    return x1,y1,x2,y2,cls

def iou(a, b):
    ax1,ay1,ax2,ay2 = a
    bx1,by1,bx2,by2 = b
    ix1,iy1 = max(ax1,bx1), max(ay1,by1)
    ix2,iy2 = min(ax2,bx2), min(ay2,by2)
    iw,ih = max(0.0, ix2-ix1), max(0.0, iy2-iy1)
    inter = iw*ih
    if inter <= 0: return 0.0
    area_a = max(0.0, ax2-ax1)*max(0.0, ay2-ay1)
    area_b = max(0.0, bx2-bx1)*max(0.0, by2-by1)
    return inter / max(1e-12, area_a + area_b - inter)

def match_boxes(A: List[BBox], B: List[BBox], iou_thresh=0.9):
    # sınıf bazlı greedy eşleştirme
    gA: Dict[int, List[Tuple[int,Tuple[float,float,float,float]]]] = {}
    gB: Dict[int, List[Tuple[int,Tuple[float,float,float,float]]]] = {}
    for i,bb in enumerate(A): x1,y1,x2,y2,c = yolo_to_xyxy(bb); gA.setdefault(c,[]).append((i,(x1,y1,x2,y2)))
    for j,bb in enumerate(B): x1,y1,x2,y2,c = yolo_to_xyxy(bb); gB.setdefault(c,[]).append((j,(x1,y1,x2,y2)))

    matches, missA, missB = [], [], []
    for c in sorted(set(gA)|set(gB)):
        Aitems = gA.get(c, []); Bitems = gB.get(c, [])
        usedA, usedB = set(), set()
        pairs = []
        for i,a in Aitems:
            for j,b in Bitems:
                pairs.append((iou(a,b), i, j))
        pairs.sort(reverse=True, key=lambda t:t[0])
        for io,i,j in pairs:
            if i in usedA or j in usedB: continue
            usedA.add(i); usedB.add(j); matches.append((i,j,c,io))
        for i,_ in Aitems:
            if i not in usedA: missB.append((i,c))
        for j,_ in Bitems:
            if j not in usedB: missA.append((j,c))
    strong = [(i,j,c,io) for (i,j,c,io) in matches if io >= iou_thresh]
    weak   = [(i,j,c,io) for (i,j,c,io) in matches if io <  iou_thresh]
    return strong, weak, missA, missB

def compare_dirs(dirA: Path, dirB: Path, out_csv: Path=None, iou_thresh=0.9):
    Amap = {p.stem:p for p in dirA.rglob("*.txt")}
    Bmap = {p.stem:p for p in dirB.rglob("*.txt")}
    names = sorted(set(Amap)|set(Bmap))

    total_s=total_w=total_mA=total_mB=0; all_ious=[]
    rows=[]
    for n in names:
        A = read_yolo_file(Amap.get(n, Path()))
        B = read_yolo_file(Bmap.get(n, Path()))
        strong, weak, missA, missB = match_boxes(A,B,iou_thresh)
        total_s += len(strong); total_w += len(weak)
        total_mA += len(missA); total_mB += len(missB)
        all_ious += [io for *_,io in strong+weak]
        mean_iou = sum(all_ious)/len(all_ious) if all_ious else 0.0
        print(f"[{n}] A:{len(A)} B:{len(B)} strong:{len(strong)} weak<{iou_thresh}:{len(weak)} "
              f"missInA:{len(missA)} missInB:{len(missB)}")
        if out_csv:
            for (i,j,c,io) in strong+weak:
                rows.append([n,c,"match",f"{io:.6f}","weak" if io<iou_thresh else "strong"])
            for (j,c) in missA: rows.append([n,c,"missing_in_A","",""])
            for (i,c) in missB: rows.append([n,c,"missing_in_B","",""])
    gmean = sum(all_ious)/len(all_ious) if all_ious else 0.0
    print("\n=== SUMMARY ===")
    print(f"matches:{total_s+total_w} strong:{total_s} weak:{total_w} "
          f"missing_in_A:{total_mA} missing_in_B:{total_mB} meanIoU:{gmean:.4f}")
    if out_csv:
        out_csv.parent.mkdir(parents=True, exist_ok=True)
        with out_csv.open("w", newline="", encoding="utf-8") as f:
            w=csv.writer(f); w.writerow(["image_stem","class_id","status","iou","strength"]); w.writerows(rows)
        print(f"CSV kaydedildi: {out_csv}")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--dirA", required=True)
    ap.add_argument("--dirB", required=True)
    ap.add_argument("--iou", type=float, default=0.9)
    ap.add_argument("--csv", type=str, default=None)
    a = ap.parse_args()
    compare_dirs(Path(a.dirA), Path(a.dirB), Path(a.csv) if a.csv else None, iou_thresh=a.iou)
