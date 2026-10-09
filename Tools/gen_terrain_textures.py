# 紅色風暴：程式產生原創、可無縫拼接的地表貼圖（顏色 + 法線）
# 用法：python gen_terrain_textures.py <輸出資料夾> [尺寸]
import sys, os
import numpy as np
from PIL import Image

OUT = sys.argv[1] if len(sys.argv) > 1 else "SourceArt/Terrain"
N = int(sys.argv[2]) if len(sys.argv) > 2 else 1024
os.makedirs(OUT, exist_ok=True)
FX = np.fft.fftfreq(N)[:, None] * N
FY = np.fft.fftfreq(N)[None, :] * N

def norm(a):
    a = a - a.min()
    return a / (a.max() + 1e-9)

def noise(seed, beta=2.0, fmin=1.0, fmax=None, ax=1.0, ay=1.0, rot=0.0):
    """頻域濾波雜訊：天生可無縫拼接。頻率單位＝每張貼圖的週期數。"""
    r = np.random.default_rng(seed)
    F = np.fft.fft2(r.standard_normal((N, N)))
    c, s = np.cos(rot), np.sin(rot)
    u = (FX * c - FY * s) * ax
    v = (FX * s + FY * c) * ay
    f = np.sqrt(u * u + v * v)
    f[0, 0] = 1.0
    amp = f ** (-beta / 2.0)
    amp[f < fmin] = 0
    if fmax: amp[f > fmax] = 0
    amp[0, 0] = 0
    return norm(np.real(np.fft.ifft2(F * amp)))

def worley(seed, count):
    """週期性 Voronoi：回傳 F1、F2（像素距離）。"""
    r = np.random.default_rng(seed)
    P = r.uniform(0, N, (count, 2))
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    F1 = np.full((N, N), 1e9, np.float32)
    F2 = np.full((N, N), 1e9, np.float32)
    ID = np.zeros((N, N), np.int32)
    for i, (py, px) in enumerate(P):
        dy = np.abs(yy - py); dy = np.minimum(dy, N - dy)
        dx = np.abs(xx - px); dx = np.minimum(dx, N - dx)
        d = np.sqrt(dx * dx + dy * dy)
        m1 = d < F1
        F2 = np.where(m1, F1, np.minimum(F2, d))
        ID = np.where(m1, i, ID)
        F1 = np.where(m1, d, F1)
    return F1, F2, ID

def warp(img, dx, dy):
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    x = (xx + dx) % N; y = (yy + dy) % N
    x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int)
    fx = x - x0; fy = y - y0
    x1 = (x0 + 1) % N; y1 = (y0 + 1) % N
    a = img[y0, x0] * (1 - fx) + img[y0, x1] * fx
    b = img[y1, x0] * (1 - fx) + img[y1, x1] * fx
    return a * (1 - fy) + b * fy

def ss(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)

def grad(v, stops):
    """依 v (0..1) 在色標間內插；stops=[(位置,(r,g,b)), ...]，顏色為 0..255 sRGB。"""
    pos = np.array([p for p, _ in stops]); col = np.array([c for _, c in stops], np.float32) / 255.0
    out = np.zeros(v.shape + (3,), np.float32)
    for k in range(3):
        out[..., k] = np.interp(v, pos, col[:, k])
    return out

def mix(a, b, t):
    return a * (1 - t[..., None]) + b * t[..., None]

def save(name, rgb, h, strength):
    rgb = np.clip(rgb, 0, 1)
    Image.fromarray((rgb * 255 + 0.5).astype(np.uint8), "RGB").save(os.path.join(OUT, f"T_{name}_A.png"))
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * strength
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * strength
    n = np.dstack([-dx, -dy, np.ones_like(h)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    # DirectX 慣例（Unreal）：綠色＝影像向下
    Image.fromarray(((n * 0.5 + 0.5) * 255 + 0.5).astype(np.uint8), "RGB").save(os.path.join(OUT, f"T_{name}_N.png"))
    m = rgb.reshape(-1, 3).mean(0) * 255
    print(f"{name}: mean sRGB {m.astype(int)}")

# ---------------- 各種地表 ----------------
def grass(name, seed, dark, mid, light, dry, dirt):
    macro = noise(seed, 2.4, 1, 12)
    mid_n = noise(seed + 1, 1.6, 6, 80)
    blades = noise(seed + 2, 1.0, 90, None, ax=1.0, ay=0.35, rot=0.6)
    blades2 = noise(seed + 3, 1.0, 90, None, ax=0.35, ay=1.0, rot=0.2)
    b = np.maximum(blades, blades2)
    b = ss(0.45, 0.85, b)
    v = np.clip(0.45 * mid_n + 0.55 * b + (macro - 0.5) * 0.35, 0, 1)
    col = grad(v, [(0, dark), (0.45, mid), (0.85, light), (1, light)])
    dryt = ss(0.55, 0.8, noise(seed + 4, 2.0, 2, 20)) * 0.55
    col = mix(col, np.array(dry, np.float32) / 255 * (0.8 + 0.4 * v[..., None]), dryt)
    # 裸露泥土斑塊 + 碎石
    patch = ss(0.18, 0.08, macro * 0.7 + mid_n * 0.3)
    F1, F2, _ = worley(seed + 5, 260)
    peb = ss(9, 3, F1)
    dcol = np.array(dirt, np.float32) / 255 * (0.8 + 0.35 * mid_n[..., None]) + peb[..., None] * 0.12
    col = mix(col, dcol, patch * (1 - b * 0.6))
    h = 0.55 * b + 0.3 * mid_n + 0.15 * macro - patch * 0.3
    save(name, col, h, 6.0)

def dirt(name, seed, dark, mid, light, stone):
    macro = noise(seed, 2.2, 1, 16)
    fine = noise(seed + 1, 1.2, 40, None)
    F1, F2, ID = worley(seed + 2, 420)
    peb = ss(10, 2, F1)
    r = np.random.default_rng(seed)
    pebv = r.uniform(0.7, 1.15, 420)[ID]
    v = np.clip(0.55 * macro + 0.45 * fine, 0, 1)
    col = grad(v, [(0, dark), (0.5, mid), (1, light)])
    sc = np.array(stone, np.float32) / 255 * pebv[..., None]
    col = mix(col, sc * (0.8 + 0.2 * (F1[..., None] < 4)), peb * 0.5)
    # 細裂紋
    crack = ss(2.5, 0.0, F2 - F1) * ss(0.4, 0.7, noise(seed + 3, 2, 2, 20))
    F1b, F2b, _ = worley(seed + 6, 60)
    crack = np.maximum(crack, ss(2.0, 0.0, F2b - F1b) * 0.7)
    col = col * (1 - 0.35 * crack[..., None])
    h = 0.35 * macro + 0.25 * fine + 0.5 * peb - 0.3 * crack
    save(name, col, h, 7.0)

def sand(name, seed, dark, mid, light, ripple=0.5, shells=False):
    macro = noise(seed, 2.4, 1, 10)
    grain = noise(seed + 1, 0.4, 150, None)
    wx = noise(seed + 2, 2.4, 1, 6) * 60
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    rp = 0.5 + 0.5 * np.sin((xx * 0.35 + yy) * (2 * np.pi * 24 / N) + wx * 0.25)
    rp = rp ** 1.6
    v = np.clip(0.55 * macro + 0.25 * grain + ripple * 0.35 * (rp - 0.5) + 0.1, 0, 1)
    col = grad(v, [(0, dark), (0.55, mid), (1, light)])
    spk = (grain > 0.82).astype(np.float32) * 0.08
    col = col + spk[..., None] - (grain < 0.12)[..., None] * 0.06
    if shells:
        F1, _, _ = worley(seed + 3, 140)
        sh = ss(4, 1, F1)
        col = mix(col, np.array([236, 228, 214], np.float32) / 255, sh * 0.8)
    h = 0.4 * macro + ripple * 0.5 * rp + 0.15 * grain
    save(name, col, h, 3.5)

def rock(name, seed, dark, mid, light, strata=0.0, moss=None):
    macro = noise(seed, 2.0, 1, 16)
    mid_n = noise(seed + 1, 1.5, 8, None)
    d1 = noise(seed + 2, 2.2, 1, 8) * 70
    d2 = noise(seed + 3, 2.2, 1, 8) * 70
    F1, F2, ID = worley(seed + 4, 70)
    F1 = warp(F1, d1, d2); F2 = warp(F2, d1, d2)
    edge = ss(6, 0, F2 - F1)
    r = np.random.default_rng(seed)
    facet = r.uniform(-0.12, 0.12, 70)[ID]
    v = np.clip(0.5 * mid_n + 0.35 * macro + facet + 0.08, 0, 1)
    if strata > 0:
        yy = np.mgrid[0:N, 0:N][0].astype(np.float32)
        st = 0.5 + 0.5 * np.sin(yy * (2 * np.pi * 14 / N) + noise(seed + 5, 2.2, 1, 6) * 9)
        v = np.clip(v * (1 - strata) + st * strata, 0, 1)
    col = grad(v, [(0, dark), (0.5, mid), (1, light)])
    col = col * (1 - 0.25 * edge[..., None])
    if moss is not None:
        mt = ss(0.6, 0.8, noise(seed + 6, 1.8, 3, 40)) * 0.6
        col = mix(col, np.array(moss, np.float32) / 255 * (0.7 + 0.5 * mid_n[..., None]), mt)
    h = 0.4 * macro + 0.35 * mid_n + facet - 0.5 * edge
    save(name, col, h, 9.0)

def snow(name, seed, dirtcol=None, ice=False):
    macro = noise(seed, 2.4, 1, 10)
    mid_n = noise(seed + 1, 1.8, 6, 60)
    grain = noise(seed + 2, 0.3, 160, None)
    v = np.clip(0.6 * macro + 0.4 * mid_n, 0, 1)
    if ice:
        col = grad(v, [(0, (150, 182, 204)), (0.5, (190, 214, 230)), (1, (225, 238, 246))])
        F1, F2, _ = worley(seed + 3, 50)
        cr = ss(2.5, 0, F2 - F1)
        col = col * (1 - 0.18 * cr[..., None]) + cr[..., None] * 0.05
        streak = noise(seed + 4, 1.0, 30, None, ax=1.0, ay=0.15, rot=0.4)
        col = col + (streak[..., None] - 0.5) * 0.08
        h = 0.3 * macro - 0.3 * cr + 0.1 * streak
        save(name, col, h, 2.5)
        return
    col = grad(v, [(0, (196, 206, 220)), (0.5, (226, 232, 240)), (1, (246, 248, 252))])
    col = col + ((grain > 0.9) * 0.06)[..., None]
    h = 0.6 * macro + 0.35 * mid_n + 0.05 * grain
    if dirtcol is not None:
        t = ss(0.6, 0.75, noise(seed + 5, 2.0, 2, 30) * 0.7 + mid_n * 0.3)
        F1, _, ID = worley(seed + 6, 300)
        peb = ss(8, 2, F1)
        dc = grad(mid_n, [(0, dirtcol[0]), (1, dirtcol[1])])
        dc = mix(dc, dc * 0.6 + 0.12, peb * 0.6)
        col = mix(col, dc, t)
        h = h - t * 0.4 + peb * t * 0.3
    save(name, col, h, 3.0)

def asphalt(name, seed):
    macro = noise(seed, 2.2, 1, 12)
    grain = noise(seed + 1, 0.2, 120, None)
    v = np.clip(0.35 * macro + 0.65 * grain, 0, 1)
    col = grad(v, [(0, (52, 53, 55)), (0.5, (78, 79, 80)), (1, (112, 112, 110))])
    patch = ss(0.5, 0.8, noise(seed + 2, 2.4, 2, 10))
    col = col * (1 - 0.1 * patch[..., None])
    F1, F2, _ = worley(seed + 3, 40)
    d1 = noise(seed + 4, 2.2, 1, 10) * 20
    cr = warp(ss(2.0, 0, F2 - F1), d1, d1.T) * ss(0.5, 0.7, noise(seed + 5, 2, 1, 8))
    col = col * (1 - 0.5 * cr[..., None])
    h = 0.4 * grain + 0.2 * macro - 0.6 * cr
    save(name, col, h, 4.0)

def macro_noise(seed):
    a = noise(seed, 2.0, 1, 24)
    Image.fromarray((a * 255).astype(np.uint8), "L").save(os.path.join(OUT, "T_Macro_A.png"))
    print("macro ok")

def water(seed):
    h = 0.55 * noise(seed, 3.0, 2, 24) + 0.3 * noise(seed + 1, 2.0, 12, 60, ax=1.0, ay=0.6, rot=0.4) + 0.15 * noise(seed + 2, 1.5, 40, 140)
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 14.0
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 14.0
    n = np.dstack([-dx, -dy, np.ones_like(h)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    Image.fromarray(((n * 0.5 + 0.5) * 255 + 0.5).astype(np.uint8), "RGB").save(os.path.join(OUT, "T_Water_N.png"))
    print("water ok")

def foam(seed):
    F1, F2, _ = worley(seed, 160)
    d1 = noise(seed + 1, 2.2, 1, 12) * 25
    e = warp(ss(7, 0, F2 - F1), d1, d1.T)
    blob = ss(0.5, 0.75, noise(seed + 2, 1.6, 4, 60))
    a = np.clip(e * 0.8 + blob * 0.6 + noise(seed + 3, 0.8, 60, None) * 0.25, 0, 1)
    Image.fromarray((a * 255).astype(np.uint8), "L").convert("RGB").save(os.path.join(OUT, "T_Foam_A.png"))
    print("foam ok")

if __name__ == "__main__":
    only = set(sys.argv[3:])
    jobs = {
        "Grass":      lambda: grass("Grass", 10, (38, 62, 24), (70, 102, 38), (128, 150, 66), (150, 140, 80), (110, 92, 66)),
        "GrassTrop":  lambda: grass("GrassTrop", 20, (28, 66, 18), (62, 116, 30), (130, 170, 56), (140, 150, 70), (126, 86, 58)),
        "Dirt":       lambda: dirt("Dirt", 30, (78, 64, 48), (112, 94, 72), (146, 128, 104), (150, 144, 134)),
        "DirtTrop":   lambda: dirt("DirtTrop", 40, (98, 60, 40), (134, 88, 60), (166, 120, 88), (160, 140, 120)),
        "Sand":       lambda: sand("Sand", 50, (168, 146, 104), (196, 176, 132), (222, 206, 166), ripple=0.35),
        "Dune":       lambda: sand("Dune", 60, (176, 150, 104), (206, 182, 136), (230, 212, 172), ripple=0.9),
        "Beach":      lambda: sand("Beach", 70, (190, 174, 136), (216, 202, 166), (238, 228, 200), ripple=0.25, shells=True),
        "Rock":       lambda: rock("Rock", 80, (78, 76, 72), (112, 110, 104), (152, 150, 142), moss=(70, 86, 46)),
        "RockTrop":   lambda: rock("RockTrop", 90, (112, 76, 52), (152, 108, 74), (190, 150, 110), strata=0.35),
        "Snow":       lambda: snow("Snow", 100),
        "SnowDirt":   lambda: snow("SnowDirt", 110, dirtcol=((84, 82, 80), (124, 120, 114))),
        "Ice":        lambda: snow("Ice", 120, ice=True),
        "Asphalt":    lambda: asphalt("Asphalt", 130),
        "Macro":      lambda: macro_noise(140),
        "Water":      lambda: water(150),
        "Foam":       lambda: foam(160),
    }
    for k, f in jobs.items():
        if not only or k in only:
            f()
