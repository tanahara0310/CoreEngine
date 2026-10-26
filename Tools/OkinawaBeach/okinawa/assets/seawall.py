"""アラハビーチの護岸と遊歩道（モジュール式、2 バリエーション）

浜の後ろの斜めのコンクリート護岸と、その上の赤茶のインターロッキングブロックの遊歩道。

座標: z=0 が遊歩道の舗装面、y=0 が護岸の天端の前面の線（監視台 lifeguard と同じ）。
-Y が海側。浜の砂は護岸の下端で z=-0.9。モジュールは X 方向に 8 m（x=-4..+4）で、
次のモジュールを x+8 に置けば継ぎ目なく並ぶ（目地も 8 m で揃う）。
  Seawall_Straight: 直線
  Seawall_Stairs  : 中央に幅 3 m の浜へ降りる階段と手すり
"""
import math

from mathutils import Vector

from .. import common as C
from .. import geo
from ..materials import concrete, galvanized, paving
from . import _timber as T

PREVIEW = dict(cam_dir=(-0.55, -1.0, 0.6), lens=40)

HALF = 4.0
SAND_Z = -0.9
TOE = -1.4               # 護岸の下端（砂に埋まる）
COPE_BACK = 0.3          # 笠木の奥行き（舗装はこの奥から）
WALK_D = 4.0             # 舗装の幅
CURB_W = 0.15
STAIR_HALF = 1.5
STEP_RISE = 0.18
STEP_RUN = 0.32


def _preview_extra(objs):
    for o in objs:
        if o.parent is None:
            o.location.z -= SAND_Z


PREVIEW["extra"] = _preview_extra


def _profile():
    """護岸（笠木 + 斜面）の断面 (y, z)。閉じた多角形"""
    pts = [(COPE_BACK, 0.02)]
    # 笠木の前の角は丸い
    r = 0.07
    for k in range(6):
        a = math.pi / 2 * k / 5
        pts.append((-0.06 + r - r * math.sin(a) - 0.0, 0.02 - r + r * math.cos(a)))
    pts.append((-0.1, -0.1))
    # 斜面（勾配 1:1）
    pts.append((-0.1 + (TOE + 0.1), TOE))
    pts.append((TOE - 0.1, TOE - 0.2))
    pts.append((COPE_BACK, TOE - 0.2))
    return pts


def wall_segment(name, x0, x1):
    rings = [[Vector((x, y, z)) for y, z in _profile()] for x in (x0, x1)]
    obj = T.loft(name, rings, cap_start=True, cap_end=True, closed=True)
    C.set_smooth(obj, True, angle=35)
    return obj


def walk(name, x0, x1, y0=COPE_BACK):
    """舗装面（1 枚の板。UV は X を 8 m、Y を舗装の幅で 0..1）"""
    y1 = COPE_BACK + WALK_D
    verts = [(x0, y0, 0), (x1, y0, 0), (x1, y1, 0), (x0, y1, 0)]
    uv = lambda x, y: ((x + HALF) / (2 * HALF), (y - COPE_BACK + 0.3) / (WALK_D + 0.3))
    uvs = [[uv(x, y) for x, y, _ in verts]]
    obj = C.mesh_object(name, verts, [(0, 1, 2, 3)], uvs)
    # 焼き先の Bake UV にも同じ UV を入れる
    bake = obj.data.uv_layers.new(name="Bake")
    for i, d in enumerate(obj.data.uv_layers["Proc"].data):
        bake.data[i].uv = d.uv
    return obj


def curb(name):
    c = geo.box(name, (2 * HALF, CURB_W, 0.33), loc=(0, COPE_BACK + WALK_D + CURB_W / 2, 0.08 - 0.165))
    geo.bevel(c, 0.015, 2)
    return c


def stairs(prefix):
    """中央の階段（踊り場 + 段 + 砂に埋まる段）と、斜面の切り口"""
    parts = []
    land = geo.box(f"{prefix}_landing", (2 * STAIR_HALF, COPE_BACK + 0.06, 0.3),
                   loc=(0, (COPE_BACK - 0.06) / 2, -0.15))
    parts.append(land)
    z = 0.0
    y = -0.06
    k = 0
    while z > TOE:
        z -= STEP_RISE
        y0, y1 = y - STEP_RUN, y
        s = geo.box(f"{prefix}_step{k}", (2 * STAIR_HALF, y1 - y0 + 0.02, 0.3), loc=(0, (y0 + y1) / 2 - 0.01, z - 0.15))
        geo.bevel(s, 0.012, 1)
        parts.append(s)
        y = y0
        k += 1
    return parts


def handrail(prefix):
    """階段の中央の手すり（亜鉛めっきのパイプ）"""
    parts = []
    ytop, ybot = 0.15, -0.06 - STEP_RUN * 5 + 0.1
    zb = SAND_Z + STEP_RISE * 0.5
    h = 0.85
    for k, (py, pz) in enumerate(((ytop, 0.0), ((ytop + ybot) / 2, (0.0 + zb) / 2 - 0.09), (ybot, zb))):
        parts.append(T.rod(f"{prefix}_post{k}", (0, py, pz - 0.1), (0, py, pz + h), 0.025, sides=12))
    pts = list(geo.bezier_points((0, ytop + 0.25, 0.0 + h - 0.35), (0, ytop + 0.25, h), (0, ytop + 0.1, h),
                                 (0, ytop, h), 6))
    pts += [Vector((0, ybot, zb + h))]
    pts += list(geo.bezier_points((0, ybot, zb + h), (0, ybot - 0.15, zb + h), (0, ybot - 0.25, zb + h - 0.05),
                                  (0, ybot - 0.25, zb + h - 0.35), 6))[1:]
    parts.append(geo.tube_along(pts, [0.024] * len(pts), sides=12, name=f"{prefix}_rail"))
    return parts


def module(prefix, with_stairs):
    walls, tops, curbs, metal = [], [], [], []
    if with_stairs:
        walls.append(wall_segment(f"{prefix}_wl", -HALF, -STAIR_HALF))
        walls.append(wall_segment(f"{prefix}_wr", STAIR_HALF, HALF))
        walls += stairs(prefix)
        metal += handrail(prefix)
    else:
        walls.append(wall_segment(f"{prefix}_w", -HALF, HALF))
    tops.append(walk(f"{prefix}_walk", -HALF, HALF))
    curbs.append(curb(f"{prefix}_curb"))
    bands = [(COPE_BACK - 0.01, COPE_BACK + 0.2, "#9e978a"), (COPE_BACK + WALK_D - 0.2, COPE_BACK + WALK_D + 0.01, "#9e978a")]
    tag = "T" if with_stairs else "S"
    groups = [
        (walls, concrete(f"SeawallConcrete{tag}", res=2048, stain_top=-0.45, salt=0.35, relief=0.009, streaks=0.55)),
        (tops, paving(f"SeawallPaving{tag}", res=2048, bands=bands, x0=-HALF, uv="keep")),
        (curbs, concrete(f"SeawallCurb{tag}", res=512, tint="#bdb9ae", salt=0.1)),
    ]
    if metal:
        groups.append((metal, galvanized(f"SeawallRail{tag}", res=256)))
    out = []
    for objs, mat in groups:
        for o in objs:
            C.assign(o, mat)
            out.append(o)
    return out


def build():
    return {
        "Seawall_Straight": module("SwS", False),
        "Seawall_Stairs": module("SwT", True),
    }
