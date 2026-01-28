import os
import glob
import math
import struct
import numpy as np
import torch
import imageio.v2 as imageio
from tqdm import tqdm
from plyfile import PlyData, PlyElement
import torch.nn.functional as F

# --- GSPLAT LIBRARY IMPORTS ---
# This uses the gsplat v1.x API.
from gsplat.rendering import rasterization

class SceneDataset:
    def __init__(self, data_dir, device="cuda"):
        self.images = []
        self.cameras = []
        self.point_cloud = []
        self.colors = []
        self.device = device

        # Find all gt_xxxxx.meta.txt files
        meta_files = sorted(glob.glob(os.path.join(data_dir, "gt_*.meta.txt")))
        print(f"Found {len(meta_files)} captures. Loading...")

        for meta_path in tqdm(meta_files):
            # Parse paths
            base_name = meta_path.replace(".meta.txt", "")
            img_path = base_name + ".png"
            depth_path = base_name + ".depth.bin"

            if not os.path.exists(img_path) or not os.path.exists(depth_path):
                continue

            # 1. LOAD METADATA
            with open(meta_path, 'r') as f:
                lines = f.readlines()
                # Parse w, h
                w = int(lines[0].strip().split()[1])
                h = int(lines[1].strip().split()[1])

                # Parse View Matrix (World -> Camera)
                view_vals = [float(x) for x in lines[2].strip().split()[1:]]
                view_mat = torch.tensor(view_vals, dtype=torch.float32).reshape(4, 4)

                # Parse Projection Matrix (Camera -> Clip)
                proj_vals = [float(x) for x in lines[3].strip().split()[1:]]
                proj_mat = torch.tensor(proj_vals, dtype=torch.float32).reshape(4, 4)

            # 2. EXTRACT INTRINSICS & EXTRINSICS
            # C2W (Camera to World) is inverse of View
            c2w = torch.inverse(view_mat)

            # Extract focal length from OpenGL Projection Matrix
            # P[0,0] = 2*fx/w  =>  fx = P[0,0] * w / 2
            # P[1,1] = 2*fy/h  =>  fy = P[1,1] * h / 2
            fx = proj_mat[0, 0] * w / 2.0
            fy = proj_mat[1, 1] * h / 2.0
            cx = w / 2.0
            cy = h / 2.0

            # 3. LOAD IMAGE
            img = imageio.imread(img_path)
            img_tensor = torch.from_numpy(img).float() / 255.0
            if img_tensor.shape[2] == 4: # Remove Alpha if present
                img_tensor = img_tensor[:, :, :3]

            # 4. LOAD DEPTH (Linear view-space Z)
            # Binary file is raw float32 array
            depth_data = np.fromfile(depth_path, dtype=np.float32).reshape(h, w)
            depth_tensor = torch.from_numpy(depth_data).to(device)

            # 5. BACK-PROJECT POINTS (For initialization)
            # We only keep a subset of points to save memory during init
            if len(self.point_cloud) < 100_000: # Limit total init points
                stride = 8 # Subsample pixels
                ys, xs = torch.meshgrid(
                    torch.arange(0, h, stride, device=device),
                    torch.arange(0, w, stride, device=device),
                    indexing='ij'
                )

                z = depth_tensor[::stride, ::stride]
                valid_mask = z > 0.01 # Filter invalid depth

                # P_cam = [(u-cx)*z/fx, (v-cy)*z/fy, z]
                x_cam = (xs - cx) * z / fx
                y_cam = (ys - cy) * z / fy
                z_cam = z

                # Stack to (N, 3)
                xyz_cam = torch.stack([x_cam[valid_mask], y_cam[valid_mask], z_cam[valid_mask]], dim=-1)

                # Transform to World: P_world = (C2W * P_cam_homo)[:3]
                # Fast manual multiply
                R = c2w[:3, :3].to(device)
                T = c2w[:3, 3].to(device)
                xyz_world = (xyz_cam @ R.T) + T

                self.point_cloud.append(xyz_world)

                # Sample colors for these points
                img_small = img_tensor[::stride, ::stride].to(device)
                self.colors.append(img_small[valid_mask])

            self.images.append(img_tensor.to(device))
            self.cameras.append({
                "c2w": c2w.to(device),
                "fx": fx, "fy": fy, "cx": cx, "cy": cy,
                "height": h, "width": w
            })

        # Concatenate all initial points
        self.init_points = torch.cat(self.point_cloud, dim=0)
        self.init_colors = torch.cat(self.colors, dim=0)
        print(f"Initialized with {self.init_points.shape[0]} points.")


class SimpleTrainer:
    def __init__(self, dataset):
        self.device = torch.device("cuda")
        self.dataset = dataset

        # --- Initialize Gaussian Parameters ---
        self.num_points = dataset.init_points.shape[0]
        self.means = torch.nn.Parameter(dataset.init_points.contiguous().requires_grad_(True))
        self.scales = torch.nn.Parameter(torch.log(torch.ones(self.num_points, 3, device=self.device) * 0.01).requires_grad_(True))
        self.quats = torch.nn.Parameter(torch.zeros(self.num_points, 4, device=self.device).requires_grad_(True))
        self.quats.data[:, 0] = 1.0 # Identity quaternion
        self.opacities = torch.nn.Parameter(torch.logit(torch.ones(self.num_points, 1, device=self.device) * 0.5).requires_grad_(True))

        # Initialize SH (Colors) - Degree 0 only for simplicity (can expand to degree 3)
        # SH degree 0 is just RGB. We convert RGB to SH coeff.
        # RGB = C0 * 0.28209... => C0 = RGB / 0.28209
        self.sh0 = torch.nn.Parameter((dataset.init_colors / 0.28209).requires_grad_(True))

        self.optimizer = torch.optim.Adam([
            {'params': [self.means], 'lr': 0.00016 * 10}, # Standard GS learning rates
            {'params': [self.scales], 'lr': 0.005},
            {'params': [self.quats], 'lr': 0.001},
            {'params': [self.opacities], 'lr': 0.05},
            {'params': [self.sh0], 'lr': 0.0025}
        ])

    def train(self, iterations=2000):
        print("Starting training...")

        pbar = tqdm(range(iterations))
        for i in pbar:
            # 1. Pick random image
            idx = np.random.randint(0, len(self.dataset.images))
            cam = self.dataset.cameras[idx]
            gt_image = self.dataset.images[idx]

            # 2. Prepare View matrix (World to Camera)
            # GSplat expects W2C. We have C2W.
            viewmat = torch.inverse(cam['c2w'])

            # 3. Project Gaussians
            # gsplat.project_gaussians takes (means, scales, glob_scale, quats, viewmat, projmat, ...)
            # We construct full projection matrix manually or pass intrinsics if supported.
            # gsplat v1 usually prefers explicit intrinsics.

            # Simple K matrix construction
            K = torch.eye(3, device=self.device)
            K[0,0], K[1,1] = cam['fx'], cam['fy']
            K[0,2], K[1,2] = cam['cx'], cam['cy']

            # Rasterization settings
            W, H = cam['width'], cam['height']

            # Render Colors
            # Decode SH
            colors_precomp = self.sh0 # Simplify: Use Degree 0 directly as color
            # Usually we use spherical harmonics here if Degree > 0

            out_img, alphas, _ = rasterization(
                self.means,
                self.quats / self.quats.norm(dim=-1, keepdim=True),
                torch.exp(self.scales),
                torch.sigmoid(self.opacities),
                colors_precomp,
                viewmat.unsqueeze(0),
                K.unsqueeze(0),
                W,
                H,
                sh_degree=0,
            )

            # 4. Loss
            loss = F.l1_loss(out_img[0], gt_image)

            # 5. Backprop
            self.optimizer.zero_grad()
            loss.backward()
            self.optimizer.step()

            if i % 100 == 0:
                pbar.set_description(f"Loss: {loss.item():.4f} | Pts: {self.num_points}")

    def save_ply(self, path):
        print(f"Saving to {path}...")
        xyz = self.means.detach().cpu().numpy()
        normals = np.zeros_like(xyz)

        # Convert SH0 back to RGB for viewing
        f_dc = self.sh0.detach().contiguous().cpu().numpy()
        rgb = (f_dc * 0.28209 + 0.5).clip(0, 1) * 255
        rgb = rgb.astype(np.uint8)

        opacities = torch.sigmoid(self.opacities).detach().cpu().numpy()
        scales = torch.exp(self.scales).detach().cpu().numpy()
        quats = self.quats.detach().cpu().numpy()

        # Construct PLY structure
        dtype_full = [('x', 'f4'), ('y', 'f4'), ('z', 'f4'),
                      ('nx', 'f4'), ('ny', 'f4'), ('nz', 'f4'),
                      ('f_dc_0', 'f4'), ('f_dc_1', 'f4'), ('f_dc_2', 'f4'),
                      ('opacity', 'f4'),
                      ('scale_0', 'f4'), ('scale_1', 'f4'), ('scale_2', 'f4'),
                      ('rot_0', 'f4'), ('rot_1', 'f4'), ('rot_2', 'f4'), ('rot_3', 'f4')]

        elements = np.empty(xyz.shape[0], dtype=dtype_full)
        elements['x'] = xyz[:, 0]
        elements['y'] = xyz[:, 1]
        elements['z'] = xyz[:, 2]
        elements['nx'] = normals[:, 0]
        elements['ny'] = normals[:, 1]
        elements['nz'] = normals[:, 2]
        elements['f_dc_0'] = f_dc[:, 0]
        elements['f_dc_1'] = f_dc[:, 1]
        elements['f_dc_2'] = f_dc[:, 2]
        elements['opacity'] = opacities[:, 0]
        elements['scale_0'] = scales[:, 0]
        elements['scale_1'] = scales[:, 1]
        elements['scale_2'] = scales[:, 2]
        elements['rot_0'] = quats[:, 0]
        elements['rot_1'] = quats[:, 1]
        elements['rot_2'] = quats[:, 2]
        elements['rot_3'] = quats[:, 3]

        el = PlyElement.describe(elements, 'vertex')
        PlyData([el]).write(path)

if __name__ == "__main__":
    # CONFIGURATION
    REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    DATA_PATH = "captures/gt"
    OUTPUT_PATH = "captures/splats_trained/scene.ply"

    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)

    # Run
    scene = SceneDataset(os.path.join(REPO_ROOT, DATA_PATH))
    trainer = SimpleTrainer(scene)
    trainer.train(iterations=2000) # Higher = better & slower
    trainer.save_ply(OUTPUT_PATH)
