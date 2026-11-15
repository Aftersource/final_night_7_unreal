"""The star backdrop's four layers as equirectangular sky maps (4096 x 2048, lat +90 at the top), from Benito's approved
look (docs/design/star_backdrop/README.md): near-black sky, the faintest navy at the bottom, dense tiny stars (mostly
white, a few #dfe6ff / #fff4e0, opacity 0.45-1), ~22 four-point sparkle stars per view with a soft blue glow; no moon,
clouds or streaks. Stars are round on the sphere (stretched by 1/cos(lat) on the map) and spread evenly over it."""
import numpy as np
from PIL import Image

W, H = 4096, 2048
rng = np.random.default_rng(7)


def sky_points(n):
    lon = rng.uniform(-180.0, 180.0, n)
    lat = np.degrees(np.arcsin(rng.uniform(-1.0, 1.0, n)))
    return (lon + 180.0) / 360.0 * W, (90.0 - lat) / 180.0 * H, lat


def splat(img, x, y, lat, sigma, color, amp, reach):
    """Add round gaussian dots (sigma in texels at the equator) with wrap in x."""
    sx = sigma / np.maximum(np.cos(np.radians(lat)), 0.08)
    rx = np.minimum(np.ceil(reach * sx).astype(int), 40)
    ry = int(np.ceil(reach * float(np.max(sigma))))
    xi, yi = np.floor(x).astype(int), np.floor(y).astype(int)
    for dy in range(-ry, ry + 2):
        for dx in range(-int(rx.max()), int(rx.max()) + 2):
            px, py = xi + dx, yi + dy
            ok = (py >= 0) & (py < H) & (np.abs(dx) <= rx + 1)
            if not ok.any():
                continue
            wgt = amp * np.exp(-0.5 * (((px + 0.5 - x) / sx) ** 2 + ((py + 0.5 - y) / sigma) ** 2))
            wgt = np.where(ok, wgt, 0.0)
            np.add.at(img, (np.clip(py, 0, H - 1), px % W), wgt[:, None] * color)


def colors(n, tint_frac):
    c = np.ones((n, 3))
    pick = rng.uniform(0, 1, n)
    c[pick < tint_frac / 2] = np.array([0xdf, 0xe6, 0xff]) / 255.0
    c[(pick >= tint_frac / 2) & (pick < tint_frac)] = np.array([0xff, 0xf4, 0xe0]) / 255.0
    return c


def save(img, name):
    a = np.clip(img, 0.0, 1.0) ** (1 / 1.0)
    Image.fromarray((a * 255.0 + 0.5).astype(np.uint8), "RGB").save(name)
    print(name, "max", float(img.max()))


# 0: the sky itself, black to the faintest navy below the horizon, and dust (the faintest, densest stars)
lat_rows = 90.0 - (np.arange(H) + 0.5) / H * 180.0
top, horizon, bottom = np.array([0, 0, 0]) / 255.0, np.array([0x02, 0x03, 0x0c]) / 255.0, np.array([0x06, 0x0a, 0x24]) / 255.0
g = np.zeros((H, 3))
for i, la in enumerate(lat_rows):
    if la >= 10.0:
        t = min(1.0, (la - 10.0) / 50.0); g[i] = horizon * (1 - t) + top * t
    elif la >= -25.0:
        t = (10.0 - la) / 35.0; g[i] = horizon * (1 - t) + bottom * t
    else:
        g[i] = bottom
L0 = np.repeat(g[:, None, :], W, axis=1).copy()
x, y, la = sky_points(26000)
splat(L0, x, y, la, np.full(len(x), 0.45), colors(len(x), 0.1), rng.uniform(0.10, 0.28, len(x)), 2.5)
save(L0, "T_Stars_0_Dust.png")

# 1: far stars, many, tiny
L1 = np.zeros((H, W, 3))
x, y, la = sky_points(30000)
splat(L1, x, y, la, np.full(len(x), 0.5), colors(len(x), 0.12), rng.uniform(0.35, 0.7, len(x)), 2.5)
save(L1, "T_Stars_1_Far.png")

# 2: mid stars, fewer, brighter, a little larger
L2 = np.zeros((H, W, 3))
x, y, la = sky_points(7000)
splat(L2, x, y, la, rng.uniform(0.55, 0.85, len(x)), colors(len(x), 0.16), rng.uniform(0.6, 1.0, len(x)), 2.6)
save(L2, "T_Stars_2_Mid.png")

# 3: near: bright stars and the four-point sparkles with a soft blue glow
L3 = np.zeros((H, W, 3))
x, y, la = sky_points(900)
splat(L3, x, y, la, rng.uniform(0.8, 1.1, len(x)), colors(len(x), 0.2), rng.uniform(0.8, 1.0, len(x)), 2.8)
n = 470
x, y, la = sky_points(n)
for i in range(n):
    s = 1.0 / max(np.cos(np.radians(la[i])), 0.08)
    arm = rng.uniform(6.0, 11.0)
    amp = rng.uniform(0.85, 1.0)
    xi, yi = int(x[i]), int(y[i])
    R = int(arm * 1.6 * s) + 3
    for dy in range(-int(arm * 1.6) - 3, int(arm * 1.6) + 4):
        for dx in range(-R, R + 1):
            px, py = (xi + dx) % W, yi + dy
            if py < 0 or py >= H:
                continue
            ux, uy = (px + 0.5 - x[i] if abs(px + 0.5 - x[i]) < W / 2 else dx) / s, py + 0.5 - y[i]
            core = np.exp(-0.5 * (ux * ux + uy * uy) / 0.9)
            arms = np.exp(-0.5 * (uy / 0.55) ** 2) * np.exp(-abs(ux) / (arm * 0.35)) + np.exp(-0.5 * (ux / 0.55) ** 2) * np.exp(-abs(uy) / (arm * 0.35))
            glow = 0.22 * np.exp(-0.5 * (ux * ux + uy * uy) / (arm * 0.55) ** 2)
            L3[py, px] += amp * ((core + 0.75 * arms) * np.array([1.0, 1.0, 1.0]) + glow * np.array([0.55, 0.7, 1.0]))
save(L3, "T_Stars_3_Near.png")
