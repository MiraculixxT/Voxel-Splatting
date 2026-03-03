import os
import glob
import math
import numpy as np
import torch
import imageio.v2 as imageio
from tqdm import tqdm
from plyfile import PlyData, PlyElement
import torch.nn.functional as F
from gsplat import rasterization

# --- HYPERPARAMETERS ---
# Tuned for sharp, voxel-like scenes
DENSIFY_START_ITER = 500
DENSIFY_END_ITER = 15_000
DENSIFY_INTERVAL = 100
PRUNE_INTERVAL = 100
OPACITY_RESET_INTERVAL = 3000

# Thresholds
#GRAD_THRESHOLD = 0.0002       # Sensitivity to high-error areas (lower = more points)
#SCALE_THRESHOLD = 0.05        # If a splat is bigger than this, split it
#OPACITY_THRESHOLD = 0.005     # Prune invisible points
GRAD_THRESHOLD = 0.001
SCALE_THRESHOLD = 0.01
OPACITY_THRESHOLD = 0.01

class SceneDataset:
    def __init__(self, data_dir, device="cuda"):
        self.images = []
        self.cameras = []
        self.point_cloud = []
        self.colors = []
        self.device = device

        meta_files = sorted(glob.glob(os.path.join(data_dir, "gt_*.meta.txt")))
        print(f"Found {len(meta_files)} captures. Loading...")

        for meta_path in tqdm(meta_files):
            base_name = meta_path.replace(".meta.txt", "")
            img_path = base_name + ".png"
            depth_path = base_name + ".depth.bin"
            if not os.path.exists(img_path) or not os.path.exists(depth_path):
                continue

            # 1. Load Metadata
            with open(meta_path, 'r') as f:
                lines = f.readlines()
                w = int(lines[0].strip().split()[1])
                h = int(lines[1].strip().split()[1])
                view_vals = [float(x) for x in lines[2].strip().split()[1:]]
                view_mat = torch.tensor(view_vals, dtype=torch.float32).reshape(4, 4)
                proj_vals = [float(x) for x in lines[3].strip().split()[1:]]
                proj_mat = torch.tensor(proj_vals, dtype=torch.float32).reshape(4, 4)

            # 2. Extract Intrinsics/Extrinsics
            c2w = torch.inverse(view_mat)
            fx = proj_mat[0, 0] * w / 2.0
            fy = proj_mat[1, 1] * h / 2.0
            cx, cy = w / 2.0, h / 2.0

            # 3. Load Image
            img = imageio.imread(img_path)
            img_tensor = torch.from_numpy(img).float() / 255.0
            if img_tensor.shape[2] == 4: img_tensor = img_tensor[:, :, :3]

            # 4. Load Depth
            depth_data = np.fromfile(depth_path, dtype=np.float32).reshape(h, w)
            depth_tensor = torch.from_numpy(depth_data).to(device)

            # 5. Back-project subset for initialization
            if len(self.point_cloud) < 100_000:
                stride = 4 # Tighter stride for better initial coverage
                ys, xs = torch.meshgrid(
                    torch.arange(0, h, stride, device=device),
                    torch.arange(0, w, stride, device=device),
                    indexing='ij'
                )
                z = depth_tensor[::stride, ::stride]
                valid_mask = z > 0.01

                x_cam = (xs - cx) * z / fx
                y_cam = (ys - cy) * z / fy
                xyz_cam = torch.stack([x_cam[valid_mask], y_cam[valid_mask], z[valid_mask]], dim=-1)

                R, T = c2w[:3, :3].to(device), c2w[:3, 3].to(device)
                xyz_world = (xyz_cam @ R.T) + T

                self.point_cloud.append(xyz_world)
                self.colors.append(img_tensor[::stride, ::stride].to(device)[valid_mask])

            self.images.append(img_tensor.to(device))

            K = torch.eye(3, device=device)
            K[0,0], K[1,1] = fx, fy
            K[0,2], K[1,2] = cx, cy

            self.cameras.append({
                "viewmat": view_mat.to(device),
                "c2w": c2w.to(device),   # <-- NEU
                "K": K,
                "width": w, "height": h
            })

        self.init_points = torch.cat(self.point_cloud, dim=0)
        self.init_colors = torch.cat(self.colors, dim=0)
        print(f"Initialized with {self.init_points.shape[0]} points.")

        # --- NORMALIZE SCENE (for huge worlds) ---
        with torch.no_grad():
            center = self.init_points.mean(dim=0)
            self.init_points = self.init_points - center

            # robust radius
            radius = torch.quantile(self.init_points.norm(dim=1), 0.95).clamp(min=1e-6)
            self.init_points = self.init_points / radius

            # apply same transform to camera translations (c2w)
            for cam in self.cameras:
                c2w = cam["c2w"]
                c2w = c2w.clone()
                c2w[:3, 3] = (c2w[:3, 3] - center) / radius

                cam["c2w"] = c2w
                cam["viewmat"] = torch.inverse(c2w)  # keep viewmat consistent

            self.scene_center = center
            self.scene_radius = radius

        print(f"Scene normalized: radius(p95)={self.scene_radius.item():.4f}")


class AdvancedTrainer:
    def __init__(self, dataset):
        self.device = torch.device("cuda")
        self.dataset = dataset

        # --- Parameters ---
        self._means = torch.nn.Parameter(dataset.init_points.contiguous().requires_grad_(True))
        self._scales = torch.nn.Parameter(torch.log(torch.ones(len(dataset.init_points), 3, device=self.device) * 0.01).requires_grad_(True))
        self._quats = torch.nn.Parameter(torch.zeros(len(dataset.init_points), 4, device=self.device).requires_grad_(True))
        self._quats.data[:, 0] = 1.0
        # start mostly transparent: sigmoid(-4) ~ 0.018
        self._opacities = torch.nn.Parameter(torch.ones(len(dataset.init_points), 1, device=self.device) * -4.0)
        self._opacities.requires_grad_(True)
        eps = 1e-6
        init_cols = dataset.init_colors.clamp(eps, 1 - eps)
        self._colors = torch.nn.Parameter(torch.logit(init_cols).requires_grad_(True))

        # Optimizer setup
        self.setup_optimizer()

        # Gradient accumulation for densification
        num_pts = dataset.init_points.shape[0]
        self.xyz_gradient_accum = torch.zeros(num_pts, device=self.device)
        self.denom = torch.zeros(num_pts, device=self.device)

    def setup_optimizer(self):
        self.optimizer = torch.optim.Adam([
            {'params': [self._means], 'lr': 0.00016 * 5.0, "name": "means"},
            {'params': [self._scales], 'lr': 0.005, "name": "scales"},
            {'params': [self._quats], 'lr': 0.001, "name": "quats"},
            {'params': [self._opacities], 'lr': 0.05, "name": "opacities"},
            {'params': [self._colors], 'lr': 0.0025, "name": "colors"}
        ])

    def train(self, iterations=7000):
        print(f"Starting training for {iterations} iterations...")
        pbar = tqdm(range(iterations))

        for i in pbar:
            # 1. Get Batch
            idx = np.random.randint(0, len(self.dataset.images))
            cam, gt_image = self.dataset.cameras[idx], self.dataset.images[idx]

            # 2. Rasterize
            viewmats = cam['viewmat'].unsqueeze(0)
            Ks = cam['K'].unsqueeze(0)

            render_colors, render_alphas, info = rasterization(
                self._means,
                self._quats / self._quats.norm(dim=-1, keepdim=True),
                torch.exp(self._scales),
                torch.sigmoid(self._opacities).squeeze(-1),
                torch.sigmoid(self._colors),
                viewmats, Ks, cam['width'], cam['height'],
                sh_degree=None,
                #Ensures info['radii'] matches the full point count
                packed=False
            )

            # IMPORTANT: Retain gradients of 2D means for densification
            if "means2d" in info:
                info["means2d"].retain_grad()

            loss = F.l1_loss(render_colors[0], gt_image)
            loss.backward()

            with torch.no_grad():
                if "means2d" in info and info["means2d"].grad is not None:
                    grads = info["means2d"].grad
                    radii = info.get("radii")

                    # gsplat may return batch-shaped outputs; normalize to 1D per-point arrays.
                    if grads is not None and grads.ndim == 3 and grads.shape[0] == 1:
                        grads = grads[0]
                    if radii is not None and radii.ndim == 2 and radii.shape[0] == 1:
                        radii = radii[0]

                    grad_norm = grads.norm(dim=-1)
                    if radii is not None and radii.ndim != 1:
                        radii = radii.reshape(-1)
                    if grad_norm.ndim != 1:
                        grad_norm = grad_norm.reshape(-1)

                    if radii is not None:
                        visible = radii > 0

                        # SAFETY CHECK:
                        if visible.shape[0] != self.xyz_gradient_accum.shape[0]:
                            print(
                                f"Shape mismatch! Mask: {visible.shape[0]}, Accum: {self.xyz_gradient_accum.shape[0]}"
                            )
                            continue # Skip this frame to prevent crash

                        self.xyz_gradient_accum[visible] += grad_norm[visible]
                        self.denom[visible] += 1

            self.optimizer.step()
            self.optimizer.zero_grad()

            # --- 5. ADAPTIVE DENSITY CONTROL ---
            if i >= DENSIFY_START_ITER and i <= DENSIFY_END_ITER:
                if i % DENSIFY_INTERVAL == 0:
                    self.densify_and_prune(GRAD_THRESHOLD, SCALE_THRESHOLD)

                if i % OPACITY_RESET_INTERVAL == 0:
                    # Reset opacities to allow re-learning
                    print("Resetting opacities...")
                    new_opacities = torch.full_like(self._opacities.data, -4.0)  # ~0.02 opacity
                    self.replace_param(self._opacities, new_opacities, "opacities")

            if i % 100 == 0:
                pbar.set_description(f"Loss: {loss.item():.4f} | Pts: {self._means.shape[0]}")

    def replace_param(self, old_param, new_tensor, name):
        new_param = torch.nn.Parameter(new_tensor.requires_grad_(True))

        for group in self.optimizer.param_groups:
            if group["name"] == name:
                # Delete old state to prevent shape mismatches in Adam
                if old_param in self.optimizer.state:
                    del self.optimizer.state[old_param]

                group["params"][0] = new_param
                # Initialize empty state for the new parameter
                self.optimizer.state[new_param] = {}

                if name == "means": self._means = new_param
                elif name == "scales": self._scales = new_param
                elif name == "quats": self._quats = new_param
                elif name == "opacities": self._opacities = new_param
                elif name == "colors": self._colors = new_param
                return

    def densify_and_prune(self, grad_threshold, scene_scale_limit):
        # Calculate average gradient per point
        grads = self.xyz_gradient_accum / self.denom.clamp(min=1)
        grads[self.denom == 0] = 0.0

        # 1. IDENTIFY CANDIDATES
        is_grad_high = grads > grad_threshold
        scales = torch.exp(self._scales)
        max_scales = torch.max(scales, dim=1).values

        # Clone: Under-reconstructed (high grad, small scale)
        should_clone = is_grad_high & (max_scales <= scene_scale_limit)

        # Split: Over-reconstructed (high grad, big scale)
        should_split = is_grad_high & (max_scales > scene_scale_limit)

        # 2. PREPARE NEW POINTS
        # Handle Clone
        cloned_means = self._means[should_clone]
        cloned_scales = self._scales[should_clone]
        cloned_quats = self._quats[should_clone]
        cloned_opacities = self._opacities[should_clone]
        cloned_colors = self._colors[should_clone]

        # Handle Split (1 into 2)
        split_scales = torch.log(scales[should_split] / 1.6)
        jitter = scales[should_split] * 0.25
        split_means_1 = self._means[should_split] + torch.randn_like(self._means[should_split]) * jitter
        split_means_2 = self._means[should_split] + torch.randn_like(self._means[should_split]) * jitter

        split_means = torch.cat([split_means_1, split_means_2], dim=0)
        split_scales = torch.cat([split_scales, split_scales], dim=0)
        split_quats = self._quats[should_split].repeat(2, 1)
        split_opacities = self._opacities[should_split].repeat(2, 1)
        split_colors = self._colors[should_split].repeat(2, 1)

        # 3. PRUNE (Transparent, Huge, or the original Split Parents)
        is_transparent = torch.sigmoid(self._opacities).squeeze() < OPACITY_THRESHOLD
        is_huge = max_scales > (scene_scale_limit * 5.0)
        should_prune = is_transparent | is_huge | should_split
        keep_mask = ~should_prune

        # 4. CONCATENATE EVERYTHING
        final_means = torch.cat([self._means[keep_mask], cloned_means, split_means], dim=0)
        final_scales = torch.cat([self._scales[keep_mask], cloned_scales, split_scales], dim=0)
        final_quats = torch.cat([self._quats[keep_mask], cloned_quats, split_quats], dim=0)
        final_opacities = torch.cat([self._opacities[keep_mask], cloned_opacities, split_opacities], dim=0)
        final_colors = torch.cat([self._colors[keep_mask], cloned_colors, split_colors], dim=0)

        # 5. UPDATE PARAMETERS AND RESET ACCUMULATORS
        # We must reset these BEFORE the next training step to avoid shape mismatch
        self.replace_param(self._means, final_means, "means")
        self.replace_param(self._scales, final_scales, "scales")
        self.replace_param(self._quats, final_quats, "quats")
        self.replace_param(self._opacities, final_opacities, "opacities")
        self.replace_param(self._colors, final_colors, "colors")

        # Create fresh accumulators matching the NEW point count
        self.xyz_gradient_accum = torch.zeros(final_means.shape[0], device=self.device)
        self.denom = torch.zeros(final_means.shape[0], device=self.device)

        torch.cuda.empty_cache()

    def save_ply(self, path):
        print(f"Saving {self._means.shape[0]} splats to {path}...")
        xyz = self._means.detach().cpu().numpy()

        rgb = torch.sigmoid(self._colors).detach().cpu().numpy()
        f_dc = (rgb - 0.5) / 0.28209

        opacities = torch.sigmoid(self._opacities).detach().cpu().numpy()
        scales = torch.exp(self._scales).detach().cpu().numpy()
        quats = self._quats.detach().cpu().numpy()

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
        elements['nx'] = np.zeros_like(xyz[:, 0])
        elements['ny'] = np.zeros_like(xyz[:, 0])
        elements['nz'] = np.zeros_like(xyz[:, 0])
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
    REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    DATA_PATH = os.path.join(REPO_ROOT, "captures/gt")
    OUTPUT_DIR = os.path.join(REPO_ROOT, "captures/splats_trained")
    OUTPUT_FILE = os.path.join(OUTPUT_DIR, "scene.ply")
    os.makedirs(os.path.dirname(OUTPUT_DIR), exist_ok=True)

    scene = SceneDataset(DATA_PATH)
    trainer = AdvancedTrainer(scene)
    trainer.train(iterations=15000) # Recommend 7k (quick) to 15k (high quality ~10min)

    trainer.save_ply(OUTPUT_FILE)
