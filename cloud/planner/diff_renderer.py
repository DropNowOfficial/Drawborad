"""Tile-partitioned PyTorch differentiable soft-capsule stroke renderer matching C++ layer compositing."""
from __future__ import annotations

import torch
import torch.nn as nn


def srgb_to_linear_torch(srgb: torch.Tensor) -> torch.Tensor:
    s = torch.clamp(srgb, 0.0, 1.0)
    return torch.where(s <= 0.04045, s / 12.92, torch.pow((s + 0.055) / 1.055, 2.4))


def linear_to_srgb_torch(lin: torch.Tensor) -> torch.Tensor:
    l = torch.clamp(lin, 0.0, 1.0)
    return torch.where(l <= 0.0031308, l * 12.92, 1.055 * torch.pow(torch.clamp(l, min=1e-8), 1.0 / 2.4) - 0.055)


class DifferentiableStrokeField(nn.Module):
    """Differentiable tapered soft-capsule strokes composited per C++ Artist Layer Grammar stage."""

    def __init__(
        self,
        init_points: torch.Tensor,   # [N, K, 2] in pixel coordinates (x, y)
        init_widths: torch.Tensor,   # [N, K] width in pixels
        init_rgb: torch.Tensor,      # [N, 3] sRGB in (0, 1)
        init_opacity: torch.Tensor,  # [N] opacity in (0, 1)
        blend_modes: list[str] | None = None,  # 'multiply', 'normal', 'screen' per stroke
        softness: torch.Tensor | float = 0.85,
    ) -> None:
        super().__init__()
        eps = 1e-4
        rgb_clamped = torch.clamp(init_rgb, eps, 1.0 - eps)
        op_clamped = torch.clamp(init_opacity, eps, 1.0 - eps)
        w_clamped = torch.clamp(init_widths, min=0.35)

        self.points = nn.Parameter(init_points.clone().float())
        self.log_widths = nn.Parameter(torch.log(w_clamped).float())
        self.rgb_logits = nn.Parameter(torch.logit(rgb_clamped).float())
        self.opacity_logits = nn.Parameter(torch.logit(op_clamped).float())
        self.blend_modes = blend_modes or ['normal'] * int(init_points.shape[0])
        if isinstance(softness, torch.Tensor):
            self.register_buffer('softness_vec', softness.clone().float().clamp(min=0.35))
        else:
            self.register_buffer('softness_vec', torch.full((int(init_points.shape[0]),), float(softness)))

    def current_parameters(self) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]:
        pts = self.points
        widths = torch.clamp(torch.exp(self.log_widths), 0.35, 64.0)
        rgb_srgb = torch.sigmoid(self.rgb_logits)
        opacity = torch.sigmoid(self.opacity_logits)
        return pts, widths, rgb_srgb, opacity

    def forward(self, base_linear: torch.Tensor, tile_size: int = 64) -> torch.Tensor:
        """Render all strokes onto base_linear [H, W, 3] using C++-faithful offscreen stage compositing."""
        h, w, _ = base_linear.shape
        device = base_linear.device
        pts, widths, rgb_srgb, opacity = self.current_parameters()
        n_strokes = int(pts.shape[0])
        if n_strokes == 0:
            return base_linear

        rgb_lin = srgb_to_linear_torch(rgb_srgb)  # [N, 3]

        with torch.no_grad():
            pad = widths.max(dim=1).values * 0.65 + self.softness_vec + 1.5
            min_xy = pts.min(dim=1).values - pad.unsqueeze(1)
            max_xy = pts.max(dim=1).values + pad.unsqueeze(1)

        rows_out: list[torch.Tensor] = []
        for y0 in range(0, h, tile_size):
            y1 = min(h, y0 + tile_size)
            cols_out: list[torch.Tensor] = []
            gy = torch.arange(y0, y1, device=device, dtype=torch.float32) + 0.5
            for x0 in range(0, w, tile_size):
                x1 = min(w, x0 + tile_size)
                active_mask = (
                    (max_xy[:, 0] >= float(x0))
                    & (min_xy[:, 0] <= float(x1))
                    & (max_xy[:, 1] >= float(y0))
                    & (min_xy[:, 1] <= float(y1))
                )
                idx = torch.nonzero(active_mask, as_tuple=False).squeeze(1)
                tile_canvas = base_linear[y0:y1, x0:x1, :]
                if idx.numel() == 0:
                    cols_out.append(tile_canvas)
                    continue

                gx = torch.arange(x0, x1, device=device, dtype=torch.float32) + 0.5
                yy, xx = torch.meshgrid(gy, gx, indexing='ij')
                grid = torch.stack([xx, yy], dim=-1)  # [th, tw, 2]
                th, tw = y1 - y0, x1 - x0

                s_pts = pts[idx]          # [M, K, 2]
                s_w = widths[idx]         # [M, K]
                s_rgb = rgb_lin[idx]      # [M, 3]
                s_op = opacity[idx]       # [M]
                s_soft = self.softness_vec[idx].view(-1, 1, 1, 1)

                a = s_pts[:, :-1, :]      # [M, S, 2]
                b = s_pts[:, 1:, :]       # [M, S, 2]
                wa = s_w[:, :-1]          # [M, S]
                wb = s_w[:, 1:]           # [M, S]
                ab = b - a                # [M, S, 2]
                len2 = (ab * ab).sum(dim=-1).clamp(min=1e-6)  # [M, S]

                ap = grid.unsqueeze(0).unsqueeze(0) - a.unsqueeze(2).unsqueeze(3)
                t = ((ap * ab.unsqueeze(2).unsqueeze(3)).sum(dim=-1) / len2.unsqueeze(2).unsqueeze(3)).clamp(0.0, 1.0)
                closest = a.unsqueeze(2).unsqueeze(3) + t.unsqueeze(-1) * ab.unsqueeze(2).unsqueeze(3)
                dist = torch.sqrt(((grid.unsqueeze(0).unsqueeze(0) - closest) ** 2).sum(dim=-1) + 1e-6)
                rad = 0.5 * (wa.unsqueeze(2).unsqueeze(3) + t * (wb - wa).unsqueeze(2).unsqueeze(3))

                seg_cov = torch.sigmoid((rad - dist) / (0.35 * s_soft))  # [M, S, th, tw]
                stroke_cov = seg_cov.max(dim=1).values  # [M, th, tw]
                alphas = (stroke_cov * s_op.view(-1, 1, 1)).unsqueeze(-1)  # [M, th, tw, 1]

                cur = tile_canvas
                idx_list = idx.tolist()

                # Offscreen accumulators for Multiply (layer opacity 0.85) and Screen (layer opacity 0.90)
                mult_prem = torch.zeros((th, tw, 3), device=device, dtype=torch.float32)
                mult_a = torch.zeros((th, tw, 1), device=device, dtype=torch.float32)
                has_mult = False

                scr_prem = torch.zeros((th, tw, 3), device=device, dtype=torch.float32)
                scr_a = torch.zeros((th, tw, 1), device=device, dtype=torch.float32)
                has_scr = False

                norm_strokes: list[int] = []
                for m_i, global_i in enumerate(idx_list):
                    mode = self.blend_modes[global_i]
                    a_m = alphas[m_i]
                    c_lin = s_rgb[m_i].view(1, 1, 3)
                    if mode == 'multiply':
                        mult_prem = c_lin * a_m + mult_prem * (1.0 - a_m)
                        mult_a = a_m + mult_a * (1.0 - a_m)
                        has_mult = True
                    elif mode == 'screen':
                        scr_prem = c_lin * a_m + scr_prem * (1.0 - a_m)
                        scr_a = a_m + scr_a * (1.0 - a_m)
                        has_scr = True
                    else:
                        norm_strokes.append(m_i)

                # 1. Composite Multiply form_glaze layer (opacity 0.85 in sRGB, matching C++ MergedDocument)
                if has_mult:
                    layer_srgb = linear_to_srgb_torch(mult_prem / torch.clamp(mult_a, min=1e-5))
                    eff_a = mult_a * 0.85
                    cur_srgb = linear_to_srgb_torch(cur)
                    cur_srgb = cur_srgb * (1.0 - eff_a) + (cur_srgb * layer_srgb) * eff_a
                    cur = srgb_to_linear_torch(cur_srgb)

                # 2. Composite Structure + Hero Ink strokes in Linear-sRGB src-over
                for m_i in norm_strokes:
                    a_m = alphas[m_i]
                    c_lin = s_rgb[m_i].view(1, 1, 3)
                    cur = cur * (1.0 - a_m) + c_lin * a_m

                # 3. Composite Screen highlights layer (opacity 0.90 in sRGB, matching C++ MergedDocument)
                if has_scr:
                    layer_srgb = linear_to_srgb_torch(scr_prem / torch.clamp(scr_a, min=1e-5))
                    eff_a = scr_a * 0.90
                    cur_srgb = linear_to_srgb_torch(cur)
                    screened = 1.0 - (1.0 - cur_srgb) * (1.0 - layer_srgb)
                    cur_srgb = cur_srgb * (1.0 - eff_a) + screened * eff_a
                    cur = srgb_to_linear_torch(cur_srgb)

                cols_out.append(cur)
            rows_out.append(torch.cat(cols_out, dim=1))
        return torch.cat(rows_out, dim=0)
