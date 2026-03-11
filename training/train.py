# training/train.py
import os
import glob
import struct
from dataclasses import dataclass
from typing import Tuple, List

import numpy as np
from PIL import Image

import torch
import torch.nn.functional as F

# -----------------------------
# Device selection
# -----------------------------
USE_CUDA = True
DEVICE = torch.device("cuda") if (USE_CUDA and torch.cuda.is_available()) else torch.device("cpu")
print("Using device:", DEVICE)


# -----------------------------
# Data structures
# -----------------------------
@dataclass
class GTFrame:
    idx: int
    rgb: torch.Tensor   # (3,H,W) float32 in [0,1]
    depth: torch.Tensor # (H,W) float32
    view: torch.Tensor  # (4,4) float32 row-major for row-vectors
    proj: torch.Tensor  # (4,4) float32 row-major for row-vectors


# -----------------------------
# IO helpers
# -----------------------------
def load_png(path: str) -> torch.Tensor:
    img = Image.open(path).convert("RGBA")
    arr = np.asarray(img, dtype=np.uint8)[..., :3].copy()
    t = torch.from_numpy(arr).float().permute(2, 0, 1).contiguous() / 255.0
    return t.to(DEVICE)


def load_depth_bin(path: str, w: int, h: int) -> torch.Tensor:
    depth = np.fromfile(path, dtype=np.float32)
    if depth.size != w * h:
        raise ValueError(f"Depth size mismatch: got {depth.size}, expected {w*h} ({w}x{h}) for {path}")
    depth = depth.reshape((h, w)).copy()
    return torch.from_numpy(depth).to(DEVICE)


def parse_meta(path: str) -> Tuple[int, int, np.ndarray, np.ndarray]:
    with open(path, "r") as f:
        lines = [ln.strip() for ln in f.readlines() if ln.strip()]

    def find(prefix: str) -> List[str]:
        for ln in lines:
            if ln.startswith(prefix + " "):
                return ln.split()[1:]
        raise ValueError(f"Missing '{prefix}' in {path}")

    w = int(find("w")[0])
    h = int(find("h")[0])

    view_vals = [float(x) for x in find("view")]
    proj_vals = [float(x) for x in find("proj")]
    if len(view_vals) != 16 or len(proj_vals) != 16:
        raise ValueError(f"Bad matrix size in {path}")

    # GLM dump is column-major; convert to row-major for:
    # clip = p4 @ view @ proj
    view = np.array(view_vals, dtype=np.float32).reshape((4, 4)).T
    proj = np.array(proj_vals, dtype=np.float32).reshape((4, 4)).T
    return w, h, view, proj


def load_first_gt_frame(gt_dir: str) -> GTFrame:
    pngs = sorted(glob.glob(os.path.join(gt_dir, "gt_*.png")))
    if not pngs:
        raise FileNotFoundError(f"No gt_*.png found in {gt_dir}")

    p = pngs[0]
    base = os.path.splitext(os.path.basename(p))[0]
    idx = int(base.split("_")[1])

    meta_path = os.path.join(gt_dir, f"{base}.meta.txt")
    depth_path = os.path.join(gt_dir, f"{base}.depth.bin")

    w, h, view_np, proj_np = parse_meta(meta_path)
    rgb = load_png(p)
    depth = load_depth_bin(depth_path, w=w, h=h)

    view = torch.from_numpy(view_np).to(DEVICE)
    proj = torch.from_numpy(proj_np).to(DEVICE)

    return GTFrame(idx=idx, rgb=rgb, depth=depth, view=view, proj=proj)


def read_spl2_chunk(path: str) -> Tuple[int, int, torch.Tensor]:
    with open(path, "rb") as f:
        header = f.read(24)
        if len(header) != 24:
            raise ValueError(f"Too small: {path}")
        magic, version, cx, cy, count, stride = struct.unpack("<IIiiII", header)
        payload = f.read()

    if magic != 0x53504C32 or version != 2:
        raise ValueError(f"Not SPL2/v2: {path} (magic={hex(magic)} ver={version})")
    if stride != 40:
        raise ValueError(f"Expected stride 40, got {stride} in {path}")
    if len(payload) != count * stride:
        raise ValueError(f"Payload mismatch: {len(payload)} vs {count*stride} in {path}")

    arr = np.frombuffer(payload, dtype=np.float32).copy()
    spl10 = torch.from_numpy(arr.reshape((count, 10))).to(DEVICE)
    return cx, cy, spl10


def load_training_splats(splats_dir: str, max_chunks: int = 32) -> torch.Tensor:
    files = sorted(glob.glob(os.path.join(splats_dir, "chunk_*_*.splats.bin")))
    if not files:
        raise FileNotFoundError(f"No chunk_*.splats.bin found in {splats_dir}")

    picked = 0
    all_spl = []
    for fp in files:
        cx, cy, spl10 = read_spl2_chunk(fp)
        if spl10.shape[0] == 0:
            continue
        all_spl.append(spl10)
        picked += 1
        if picked >= max_chunks:
            break

    if not all_spl:
        raise RuntimeError("No non-empty chunks found.")

    out = torch.cat(all_spl, dim=0).contiguous()
    print(f"Loaded {picked} chunks -> total splats: {out.shape[0]}")
    return out


def write_spl2_chunk(path: str, cx: int, cy: int, splats10: torch.Tensor):
    os.makedirs(os.path.dirname(path), exist_ok=True)

    spl_cpu = splats10.detach().to("cpu").contiguous()
    if spl_cpu.dtype != torch.float32:
        spl_cpu = spl_cpu.float()

    count = int(spl_cpu.shape[0])
    stride = 40
    magic = 0x53504C32
    version = 2

    header = struct.pack("<IIiiII", magic, version, int(cx), int(cy), count, stride)
    payload = spl_cpu.numpy().astype(np.float32, copy=False).tobytes(order="C")

    expected = count * stride
    if len(payload) != expected:
        raise ValueError(f"Bad payload bytes: {len(payload)} vs {expected}")

    with open(path, "wb") as f:
        f.write(header)
        f.write(payload)


def save_tensor_image(path: str, img_3hw: torch.Tensor):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img = img_3hw.detach().clamp(0, 1).permute(1, 2, 0).cpu().numpy()
    Image.fromarray((img * 255.0).astype(np.uint8)).save(path)


# -----------------------------
# Projection
# -----------------------------
def project(pos_world: torch.Tensor, view: torch.Tensor, proj: torch.Tensor):
    """
    Row-vector convention:
      clip = p4 @ view @ proj
    view/proj are row-major.
    """
    N = pos_world.shape[0]
    ones = torch.ones((N, 1), device=pos_world.device, dtype=pos_world.dtype)
    p4 = torch.cat([pos_world, ones], dim=1)  # (N,4)
    clip = (p4 @ view) @ proj                 # (N,4)

    w = clip[:, 3:4]
    w_safe = torch.where(w.abs() < 1e-8, torch.full_like(w, 1e-8), w)
    ndc = clip[:, :3] / w_safe
    return ndc


# -----------------------------
# Manual-grad renderer/trainer
# -----------------------------
@torch.no_grad()
def render_and_accumulate(
        pos: torch.Tensor,
        color: torch.Tensor,    # (N,3) in [0,1]
        opacity: torch.Tensor,  # (N,1) in [0,1]
        view: torch.Tensor,
        proj: torch.Tensor,
        H: int,
        W: int,
        max_splats: int,
        R: int,
):
    """
    Forward pass WITHOUT autograd.
    Returns:
      pred (3,H,W),
      acc_rgb (3,H,W)  (numerator),
      acc_a  (1,H,W)   (denominator),
      used indices and their per-splat projected x,y (for backward pass)
    """
    N = pos.shape[0]
    if N > max_splats:
        idx = torch.randperm(N, device=pos.device)[:max_splats]
        pos = pos[idx]
        color = color[idx]
        opacity = opacity[idx]
    else:
        idx = None  # means all

    ndc = project(pos, view, proj)

    mask = torch.isfinite(ndc).all(dim=1)
    mask = mask & (ndc[:, 0].abs() < 2.0) & (ndc[:, 1].abs() < 2.0) & (ndc[:, 2] > -2.0) & (ndc[:, 2] < 2.0)

    kept = int(mask.sum().item())
    print("mask kept:", kept, "/", int(mask.numel()))
    if kept == 0:
        z = torch.zeros((3, H, W), device=pos.device, dtype=torch.float32)
        a = torch.zeros((1, H, W), device=pos.device, dtype=torch.float32)
        return z, z.clone(), a, None

    ndc = ndc[mask]
    color = color[mask].clamp(0, 1)
    opacity = opacity[mask].clamp(0, 1).squeeze(1)  # (K,)

    x = (ndc[:, 0] * 0.5 + 0.5) * (W - 1)
    y = (1.0 - (ndc[:, 1] * 0.5 + 0.5)) * (H - 1)

    K = int(x.shape[0])
    print("to splat:", K)

    sigma2 = float(R * R)

    acc_rgb = torch.zeros((3, H, W), device=pos.device, dtype=torch.float32)
    acc_a = torch.zeros((1, H, W), device=pos.device, dtype=torch.float32)

    for i in range(K):
        xi = x[i].item()
        yi = y[i].item()

        cx = int(round(xi))
        cy = int(round(yi))

        x0 = max(0, cx - R)
        x1 = min(W - 1, cx + R)
        y0 = max(0, cy - R)
        y1 = min(H - 1, cy + R)
        if x1 < x0 or y1 < y0:
            continue

        xs = torch.arange(x0, x1 + 1, device=pos.device, dtype=torch.float32)
        ys = torch.arange(y0, y1 + 1, device=pos.device, dtype=torch.float32)
        yy, xx = torch.meshgrid(ys, xs, indexing="ij")

        dx = xx - float(xi)
        dy = yy - float(yi)
        wgt = torch.exp(-(dx * dx + dy * dy) / (2.0 * sigma2 + 1e-6))  # (h,w)

        a = (opacity[i].item() * wgt).clamp(0, 1)  # (h,w)

        acc_rgb[:, y0:y1 + 1, x0:x1 + 1] += a.unsqueeze(0) * color[i].view(3, 1, 1)
        acc_a[:, y0:y1 + 1, x0:x1 + 1] += a.unsqueeze(0)

    pred = acc_rgb / acc_a.clamp_min(1e-6)
    pred = pred.clamp(0, 1)
    # store x,y,mask for backward
    aux = (idx, mask, x, y)
    return pred, acc_rgb, acc_a, aux


@torch.no_grad()
def manual_backward_update(
        pos: torch.Tensor,
        color: torch.Tensor,    # (N,3)
        opacity: torch.Tensor,  # (N,1)
        gt_rgb: torch.Tensor,   # (3,H,W)
        view: torch.Tensor,
        proj: torch.Tensor,
        H: int,
        W: int,
        max_splats: int,
        R: int,
        lr: float,
):
    """
    Computes gradients manually (MSE) and applies SGD step to color/opacity.
    """
    pred, acc_rgb, acc_a, aux = render_and_accumulate(pos, color, opacity, view, proj, H, W, max_splats, R)
    if aux is None:
        return pred, float(((pred - gt_rgb) ** 2).mean().item())

    # MSE loss
    diff = (pred - gt_rgb)
    loss = (diff * diff).mean()
    loss_val = float(loss.item())

    # dL/dpred
    # mean over (3*H*W)
    dL_dpred = (2.0 / (3.0 * H * W)) * diff  # (3,H,W)

    idx_sub, mask, x, y = aux

    # We need the exact tensors used in forward after subsample+mask.
    N = pos.shape[0]
    if N > max_splats:
        idx = idx_sub
        pos_s = pos[idx]
        color_s = color[idx]
        op_s = opacity[idx].squeeze(1)
    else:
        pos_s = pos
        color_s = color
        op_s = opacity.squeeze(1)

    # apply mask
    pos_k = pos_s[mask]
    color_k = color_s[mask].clamp(0, 1)
    op_k = op_s[mask].clamp(0, 1)  # (K,)

    K = int(x.shape[0])
    sigma2 = float(R * R)

    # grads for the selected, masked set
    g_color = torch.zeros_like(color_k)
    g_op = torch.zeros_like(op_k)

    # We need pred + denom per pixel for the quotient derivative
    denom = acc_a.clamp_min(1e-6)     # (1,H,W)
    pred_local = pred                 # (3,H,W)

    for i in range(K):
        xi = x[i].item()
        yi = y[i].item()

        cx = int(round(xi))
        cy = int(round(yi))

        x0 = max(0, cx - R)
        x1 = min(W - 1, cx + R)
        y0 = max(0, cy - R)
        y1 = min(H - 1, cy + R)
        if x1 < x0 or y1 < y0:
            continue

        xs = torch.arange(x0, x1 + 1, device=pos.device, dtype=torch.float32)
        ys = torch.arange(y0, y1 + 1, device=pos.device, dtype=torch.float32)
        yy, xx = torch.meshgrid(ys, xs, indexing="ij")

        dx = xx - float(xi)
        dy = yy - float(yi)
        wgt = torch.exp(-(dx * dx + dy * dy) / (2.0 * sigma2 + 1e-6))  # (h,w)

        # effective alpha contribution for this splat at pixels
        a = (op_k[i].item() * wgt).clamp(0, 1)  # (h,w)

        # slices
        dL = dL_dpred[:, y0:y1 + 1, x0:x1 + 1]      # (3,h,w)
        den = denom[:, y0:y1 + 1, x0:x1 + 1]        # (1,h,w)
        pr  = pred_local[:, y0:y1 + 1, x0:x1 + 1]   # (3,h,w)

        # d pred / d color_i  = a/den  (per channel)
        coeff = (a / den.squeeze(0)).clamp(0, 1e6)  # (h,w)
        # g_color_i[c] += sum_{pixels} dL_dpred[c] * coeff
        for c in range(3):
            g_color[i, c] += (dL[c] * coeff).sum()

        # d pred_c / d opacity_i = wgt/den * (color_c - pred_c)
        coeff2 = (wgt / den.squeeze(0)).clamp(0, 1e6)  # (h,w)
        g_op[i] += (dL * (color_k[i].view(3, 1, 1) - pr) * coeff2).sum()

    # Apply SGD update back to original tensors
    # Map gradients back through subsampling + mask
    if N > max_splats:
        # indices of chosen splats in original
        chosen = idx_sub
        chosen_masked = chosen[mask]  # (K,)
        color[chosen_masked] -= lr * g_color
        opacity[chosen_masked, 0] -= lr * g_op
    else:
        # all splats used; mask indexes directly
        mask_idx = torch.nonzero(mask, as_tuple=False).squeeze(1)
        color[mask_idx] -= lr * g_color
        opacity[mask_idx, 0] -= lr * g_op

    # clamp parameters
    color.clamp_(0, 1)
    opacity.clamp_(0, 1)

    return pred, loss_val


# -----------------------------
# Main
# -----------------------------
def main():
    gt = load_first_gt_frame("captures/gt")
    print(f"Loaded GT frame {gt.idx} rgb={tuple(gt.rgb.shape)} depth={tuple(gt.depth.shape)}")

    spl10 = load_training_splats("captures/splats_v2", max_chunks=32)

    pos = spl10[:, 0:3].contiguous()
    scale = spl10[:, 3:6].contiguous()

    # We only train color+opacity for now (CPU debug)
    color = spl10[:, 6:9].clamp(0, 1).clone().contiguous()
    opacity = spl10[:, 9:10].clamp(0, 1).clone().contiguous()

    # Smaller render resolution
    H, W = 180, 320
    gt_rgb_small = F.interpolate(gt.rgb.unsqueeze(0), size=(H, W), mode="bilinear", align_corners=False).squeeze(0)

    # Forward before
    pred0, _, _, _ = render_and_accumulate(pos, color, opacity, gt.view, gt.proj, H, W, max_splats=4000, R=3)
    save_tensor_image("out/debug_before.png", pred0)

    # Manual training
    steps = 100
    lr = 5e-2  # manual SGD; adjust if needed
    for it in range(steps):
        pred, loss_val = manual_backward_update(
            pos=pos,
            color=color,
            opacity=opacity,
            gt_rgb=gt_rgb_small,
            view=gt.view,
            proj=gt.proj,
            H=H, W=W,
            max_splats=4000,
            R=3,
            lr=lr
        )
        if it % 10 == 0 or it == steps - 1:
            print(f"iter {it:04d} loss={loss_val:.6f}")

    # After
    pred1, _, _, _ = render_and_accumulate(pos, color, opacity, gt.view, gt.proj, H, W, max_splats=4000, R=3)
    save_tensor_image("out/debug_after.png", pred1)

    # Export trained splats (keep pos/scale fixed)
    trained = torch.cat([pos, scale, color, opacity], dim=1).contiguous()
    out_path = os.path.join("captures/splats_trained", "region_trained.splats.bin")
    write_spl2_chunk(out_path, 0, 0, trained)

    print("Wrote out/debug_before.png and out/debug_after.png")
    print("Exported trained splats to:", out_path)


if __name__ == "__main__":
    main()
