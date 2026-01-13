

import os
import glob
import struct
from dataclasses import dataclass
from typing import Tuple, List

import numpy as np
from PIL import Image
import torch
import torch.nn.functional as F

# ---- Device selection ----
USE_CUDA = False  # set True on a CUDA machine
if USE_CUDA and torch.cuda.is_available():
    DEVICE = torch.device("cuda")
else:
    DEVICE = torch.device("cpu")
print("Using device:", DEVICE)


@dataclass
class GTFrame:
    idx: int
    rgb: torch.Tensor   # (3,H,W) float32
    depth: torch.Tensor # (H,W) float32 (linear)
    view: torch.Tensor  # (4,4) float32
    proj: torch.Tensor  # (4,4) float32


@dataclass
class ChunkSplats:
    cx: int
    cy: int
    splats10: torch.Tensor  # (N,10) float32 [pos3, scale3, color3, opacity]


def load_png(path: str) -> torch.Tensor:
    img = Image.open(path).convert("RGBA")
    arr = np.asarray(img, dtype=np.uint8)[..., :3]  # (H,W,3)
    t = torch.from_numpy(arr).float().permute(2, 0, 1) / 255.0
    return t.to(DEVICE)


def load_depth_bin(path: str, w: int, h: int) -> torch.Tensor:
    depth = np.fromfile(path, dtype=np.float32)
    if depth.size != w * h:
        raise ValueError(f"Depth size mismatch: {depth.size} vs {w*h} for {path}")
    depth = depth.reshape((h, w))
    return torch.from_numpy(depth).to(DEVICE)


def parse_meta(path: str) -> Tuple[int, int, np.ndarray, np.ndarray]:
    # format written by the engine:
    # w <int>\n
    # h <int>\n
    # view <16 floats>\n
    # proj <16 floats>\n
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

    # Note: the engine wrote view[c][r] (column-major). We keep the same ordering.
    view = np.array(view_vals, dtype=np.float32).reshape((4, 4))
    proj = np.array(proj_vals, dtype=np.float32).reshape((4, 4))
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
    # Header:
    # magic(u32), version(u32), cx(i32), cy(i32), count(u32), stride(u32)
    # Payload (v2): count * 10 float32 (stride=40)
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

    arr = np.frombuffer(payload, dtype=np.float32)
    splats10 = torch.from_numpy(arr.reshape((count, 10))).to(DEVICE)
    return cx, cy, splats10


# --- Write SPL2/v2 chunk helper ---
def write_spl2_chunk(path: str, cx: int, cy: int, splats10: torch.Tensor):
    """Write SPL2/v2 chunk file.

    splats10: (N,10) float32 [pos3, scale3, color3, opacity]
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)

    spl_cpu = splats10.detach().to("cpu").contiguous()
    if spl_cpu.dtype != torch.float32:
        spl_cpu = spl_cpu.float()

    count = int(spl_cpu.shape[0])
    stride = 40  # 10 * float32

    # Header matches C++: magic(u32), version(u32), cx(i32), cy(i32), count(u32), stride(u32)
    magic = 0x53504C32  # 'SPL2'
    version = 2

    header = struct.pack("<IIiiII", magic, version, int(cx), int(cy), count, stride)

    payload = spl_cpu.numpy().astype(np.float32, copy=False).tobytes(order="C")
    expected = count * stride
    if len(payload) != expected:
        raise ValueError(f"Bad payload bytes: {len(payload)} vs {expected}")

    with open(path, "wb") as f:
        f.write(header)
        f.write(payload)


def load_one_nonempty_chunk(splats_dir: str) -> ChunkSplats:
    files = sorted(glob.glob(os.path.join(splats_dir, "chunk_*_*.splats.bin")))
    if not files:
        raise FileNotFoundError(f"No chunk_*.splats.bin found in {splats_dir}")

    for fp in files:
        cx, cy, spl10 = read_spl2_chunk(fp)
        if spl10.shape[0] > 0:
            return ChunkSplats(cx=cx, cy=cy, splats10=spl10)

    raise RuntimeError("All chunk splat files were empty")


def save_tensor_image(path: str, img_3hw: torch.Tensor):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img = img_3hw.detach().clamp(0, 1).permute(1, 2, 0).cpu().numpy()
    Image.fromarray((img * 255.0).astype(np.uint8)).save(path)


def project(pos_world: torch.Tensor, view: torch.Tensor, proj: torch.Tensor):
    # pos_world: (N,3)
    N = pos_world.shape[0]
    ones = torch.ones((N, 1), device=pos_world.device, dtype=pos_world.dtype)
    p4 = torch.cat([pos_world, ones], dim=1)  # (N,4)

    # The meta matrices are in column-major ordering (GLM). Using transpose here matches the earlier sanity loader.
    clip = (p4 @ view.T) @ proj.T  # (N,4)
    w = clip[:, 3:4].clamp_min(1e-8)
    ndc = clip[:, :3] / w
    return ndc, w.squeeze(1)


def render_gaussian_splats(
    pos: torch.Tensor,
    scale: torch.Tensor,
    color: torch.Tensor,
    opacity: torch.Tensor,
    view: torch.Tensor,
    proj: torch.Tensor,
    H: int,
    W: int,
    max_splats: int = 5000,
):
    """Differentiable (but slow) debug renderer.

    - Projects splat centers.
    - Uses a simple screen-space radius derived from average world scale and depth.
    - Alpha-composites front-to-back.

    This is for verification + first tiny training runs.
    """

    # Subsample for speed
    N = pos.shape[0]
    if N > max_splats:
        idx = torch.randperm(N, device=pos.device)[:max_splats]
        pos = pos[idx]
        scale = scale[idx]
        color = color[idx]
        opacity = opacity[idx]

    ndc, wclip = project(pos, view, proj)

    # Keep in front + roughly in view
    mask = (wclip > 0) & (ndc[:, 2] > -1) & (ndc[:, 2] < 1) & (ndc[:, 0].abs() < 1.2) & (ndc[:, 1].abs() < 1.2)
    ndc = ndc[mask]
    color = color[mask]
    opacity = opacity[mask]
    scale = scale[mask]

    # Screen coords
    x = (ndc[:, 0] * 0.5 + 0.5) * (W - 1)
    y = (1.0 - (ndc[:, 1] * 0.5 + 0.5)) * (H - 1)
    z = ndc[:, 2]

    # Sort front-to-back
    order = torch.argsort(z)
    x = x[order]
    y = y[order]
    z = z[order]
    color = color[order]
    opacity = opacity[order]
    scale = scale[order]

    # Very rough screen radius (debug): depends on scale and depth
    # Bigger world scale -> bigger splat. Farther -> smaller.
    avg_scale = scale.mean(dim=1).clamp_min(0.01)
    # Map ndc z [-1..1] to something positive-ish for sizing
    depth_factor = (1.5 - (z * 0.5 + 0.5)).clamp(0.25, 2.0)
    r = (avg_scale * 30.0 * depth_factor).clamp(1.0, 20.0)

    img = torch.zeros((3, H, W), device=pos.device, dtype=torch.float32)
    a_acc = torch.zeros((1, H, W), device=pos.device, dtype=torch.float32)

    # Splat each point into a local patch
    for i in range(x.shape[0]):
        xi = x[i]
        yi = y[i]
        ri = int(torch.round(r[i]).item())
        if ri <= 0:
            continue

        x0 = max(0, int(torch.floor(xi).item()) - ri)
        x1 = min(W - 1, int(torch.floor(xi).item()) + ri)
        y0 = max(0, int(torch.floor(yi).item()) - ri)
        y1 = min(H - 1, int(torch.floor(yi).item()) + ri)

        xs = torch.arange(x0, x1 + 1, device=pos.device, dtype=torch.float32)
        ys = torch.arange(y0, y1 + 1, device=pos.device, dtype=torch.float32)
        yy, xx = torch.meshgrid(ys, xs, indexing="ij")

        dx = xx - xi
        dy = yy - yi

        sigma2 = (ri * 0.5) ** 2
        wgt = torch.exp(-(dx * dx + dy * dy) / (2.0 * sigma2 + 1e-6))

        a = (opacity[i].clamp(0, 1) * wgt).clamp(0, 1)

        prev_a = a_acc[:, y0 : y1 + 1, x0 : x1 + 1]
        one_minus = (1.0 - prev_a)

        # front-to-back compositing
        img[:, y0 : y1 + 1, x0 : x1 + 1] = img[:, y0 : y1 + 1, x0 : x1 + 1] + one_minus * a * color[i].view(3, 1, 1)
        a_acc[:, y0 : y1 + 1, x0 : x1 + 1] = prev_a + one_minus * a

    return img.clamp(0, 1)


def main():
    gt = load_first_gt_frame("captures/gt")
    print(f"Loaded GT frame {gt.idx} rgb={tuple(gt.rgb.shape)} depth={tuple(gt.depth.shape)}")

    # Use SPL2 dumps
    spl_chunk = load_one_nonempty_chunk("captures/splats_v2")
    print(f"Loaded chunk ({spl_chunk.cx},{spl_chunk.cy}) splats={spl_chunk.splats10.shape[0]}")

    # Build tensors
    spl10 = spl_chunk.splats10
    pos = spl10[:, 0:3].contiguous()
    scale = spl10[:, 3:6].contiguous()

    # Trainable params: color + opacity
    color_init = spl10[:, 6:9].clamp(0, 1)
    opacity_init = spl10[:, 9:10].clamp(0, 1)

    color = torch.nn.Parameter(color_init.clone())
    opacity = torch.nn.Parameter(opacity_init.clone())

    # Render at a smaller resolution for speed
    H, W = 180, 320
    gt_rgb_small = F.interpolate(gt.rgb.unsqueeze(0), size=(H, W), mode="bilinear", align_corners=False).squeeze(0)

    # Before
    img0 = render_gaussian_splats(pos, scale, color.clamp(0, 1), opacity.clamp(0, 1), gt.view, gt.proj, H, W, max_splats=4000)
    save_tensor_image("out/debug_before.png", img0)

    # Train (tiny first run)
    opt = torch.optim.Adam([color, opacity], lr=1e-2)

    steps = 100
    for it in range(steps):
        opt.zero_grad(set_to_none=True)

        pred = render_gaussian_splats(pos, scale, color.clamp(0, 1), opacity.clamp(0, 1), gt.view, gt.proj, H, W, max_splats=4000)
        loss = (pred - gt_rgb_small).abs().mean()

        loss.backward()
        opt.step()

        # Keep params in range (in-place clamp on .data for simplicity)
        color.data.clamp_(0, 1)
        opacity.data.clamp_(0, 1)

        if (it % 10) == 0 or it == steps - 1:
            print(f"iter {it:04d} loss={loss.item():.6f}")

    # After
    img1 = render_gaussian_splats(pos, scale, color.clamp(0, 1), opacity.clamp(0, 1), gt.view, gt.proj, H, W, max_splats=4000)
    save_tensor_image("out/debug_after.png", img1)

    # --- Export trained splats (SPL2/v2) ---
    trained = torch.cat([
        pos,
        scale,
        color.clamp(0, 1),
        opacity.clamp(0, 1)
    ], dim=1).contiguous()

    out_dir = "captures/splats_trained"
    out_path = os.path.join(out_dir, f"chunk_{spl_chunk.cx}_{spl_chunk.cy}.splats.bin")
    write_spl2_chunk(out_path, spl_chunk.cx, spl_chunk.cy, trained)

    print("Wrote out/debug_before.png and out/debug_after.png")
    print(f"Exported trained splats to: {out_path}")


if __name__ == "__main__":
    main()