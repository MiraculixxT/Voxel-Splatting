import os
import glob
import struct
from dataclasses import dataclass
from typing import Dict, List, Tuple

import numpy as np
from PIL import Image
# ---- Device selection ----
import torch
USE_CUDA = False  # set to True to use CUDA, False for CPU

if USE_CUDA and torch.cuda.is_available():
    DEVICE = torch.device("cuda")
else:
    DEVICE = torch.device("cpu")

print("Using device:", DEVICE)


@dataclass
class GTFrame:
    idx: int
    rgb: torch.Tensor       # (3,H,W) float32 in [0,1], on CUDA
    depth: torch.Tensor     # (H,W) float32 linear depth, on CUDA
    view: torch.Tensor      # (4,4) float32, on CUDA
    proj: torch.Tensor      # (4,4) float32, on CUDA


@dataclass
class ChunkSplatsRaw:
    cx: int
    cy: int
    count: int
    stride: int
    raw: bytes  # count*stride bytes


def load_png_cuda(path: str) -> torch.Tensor:
    img = Image.open(path).convert("RGBA")  # same as your dump
    arr = np.asarray(img, dtype=np.uint8)  # (H,W,4)
    rgb = arr[..., :3].astype(np.float32) / 255.0
    t = torch.from_numpy(rgb).permute(2, 0, 1).contiguous()  # (3,H,W)
    return t.to(DEVICE)


def load_depth_bin_cuda(path: str, w: int, h: int) -> torch.Tensor:
    # float32 array of size w*h, row-major, already vertically flipped in C++
    depth = np.fromfile(path, dtype=np.float32)
    if depth.size != w * h:
        raise ValueError(f"Depth size mismatch: got {depth.size}, expected {w*h} ({w}x{h}) for {path}")
    depth = depth.reshape((h, w))
    t = torch.from_numpy(depth).contiguous()
    return t.to(DEVICE)


def parse_meta(path: str) -> Tuple[int, int, np.ndarray, np.ndarray]:
    # meta format:
    # w <int>
    # h <int>
    # view <16 floats>
    # proj <16 floats>
    with open(path, "r") as f:
        lines = [ln.strip() for ln in f.readlines() if ln.strip()]

    def parse_line(prefix: str) -> List[float]:
        for ln in lines:
            if ln.startswith(prefix + " "):
                parts = ln.split()
                return [float(x) for x in parts[1:]]
        raise ValueError(f"Missing '{prefix}' in {path}")

    w = int(parse_line("w")[0])
    h = int(parse_line("h")[0])

    view_vals = parse_line("view")
    proj_vals = parse_line("proj")
    if len(view_vals) != 16 or len(proj_vals) != 16:
        raise ValueError(f"Bad matrix length in {path}: view {len(view_vals)} proj {len(proj_vals)}")

    # IMPORTANT: you wrote view[c][r] in C++ => column-major order in the text
    # We reconstruct a (4,4) matrix M such that M[c,r] matches those values.
    view = np.array(view_vals, dtype=np.float32).reshape((4, 4))
    proj = np.array(proj_vals, dtype=np.float32).reshape((4, 4))
    return w, h, view, proj


def load_gt_frames(gt_dir: str, max_frames: int = 8) -> List[GTFrame]:
    pngs = sorted(glob.glob(os.path.join(gt_dir, "gt_*.png")))
    frames: List[GTFrame] = []

    for p in pngs[:max_frames]:
        base = os.path.splitext(os.path.basename(p))[0]  # gt_00012
        idx = int(base.split("_")[1])

        meta_path = os.path.join(gt_dir, f"{base}.meta.txt")
        depth_path = os.path.join(gt_dir, f"{base}.depth.bin")

        w, h, view_np, proj_np = parse_meta(meta_path)

        rgb = load_png_cuda(p)
        depth = load_depth_bin_cuda(depth_path, w=w, h=h)

        view = torch.from_numpy(view_np).to(DEVICE)
        proj = torch.from_numpy(proj_np).to(DEVICE)

        frames.append(GTFrame(idx=idx, rgb=rgb, depth=depth, view=view, proj=proj))

    return frames


def read_splats_bin(path: str) -> ChunkSplatsRaw:
    # Header written in C++:
    # magic(u32) version(u32) cx(i32) cy(i32) count(u32) stride(u32) then raw bytes
    with open(path, "rb") as f:
        header = f.read(24)
        if len(header) != 24:
            raise ValueError(f"Too small splat file: {path}")
        magic, version, cx, cy, count, stride = struct.unpack("<IIiiII", header)
        # Support both SPLT (v1) and SPL2 (v2) for error clarity
        if magic == 0x53504C54:
            fmt_version = 1
        elif magic == 0x53504C32:
            fmt_version = 2
        else:
            raise ValueError(f"Bad magic in {path}: {hex(magic)}")
        raw = f.read()
        expected = int(count) * int(stride)
        if len(raw) != expected:
            raise ValueError(f"Raw size mismatch in {path}: got {len(raw)} expected {expected} (count={count}, stride={stride})")

    return ChunkSplatsRaw(cx=cx, cy=cy, count=count, stride=stride, raw=raw)


def load_all_splats(splats_dir: str, max_chunks: int = 8) -> Dict[Tuple[int, int], ChunkSplatsRaw]:
    files = sorted(glob.glob(os.path.join(splats_dir, "chunk_*_*.splats.bin")))

    print("Found splat files:")
    for f in files:
        print("  ", f)

    out: Dict[Tuple[int, int], ChunkSplatsRaw] = {}
    for fp in files:
        cs = read_splats_bin(fp)
        if cs.count > 0:
            out[(cs.cx, cs.cy)] = cs
            print(f"Using chunk ({cs.cx},{cs.cy}) splats={cs.count}")
        if len(out) >= max_chunks:
            break

    return out


def main():
    gt_dir = "captures/gt"
    splats_dir = "captures/splats_v2"

    print("Splats dir (rel):", splats_dir)
    print("Splats dir (abs):", os.path.abspath(splats_dir))

    frames = load_gt_frames(gt_dir, max_frames=5)
    print(f"Loaded {len(frames)} GT frames")

    if frames:
        f0 = frames[0]
        print(f"Frame {f0.idx}: rgb {tuple(f0.rgb.shape)} depth {tuple(f0.depth.shape)}")
        print(f"RGB min/max: {f0.rgb.min().item():.4f}/{f0.rgb.max().item():.4f}")
        print(f"Depth min/max: {f0.depth.min().item():.4f}/{f0.depth.max().item():.4f}")
        print("View[0,:]:", f0.view[0].detach().cpu().numpy())
        print("Proj[0,:]:", f0.proj[0].detach().cpu().numpy())

    splats = load_all_splats(splats_dir, max_chunks=5)
    print(f"Loaded {len(splats)} chunk splat blobs (raw, not decoded)")
    for (cx, cy), cs in splats.items():
        print(f"  chunk ({cx},{cy}) count={cs.count} stride={cs.stride} bytes={len(cs.raw)}")

    print("OK: dataset + splat dumps readable on GPU.")


if __name__ == "__main__":
    main()