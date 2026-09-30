"""アダン（Pandanus odoratissimus）2 バリエーション

幹: 二又に分かれる灰褐色の幹（葉痕のリング）+ 下部から砂へ斜めに伸びる支柱根
葉: 細長い帯状の葉を V 字断面の短冊ジオメトリで作り、枝先にらせん状のロゼットとして付ける
    アトラス（8 列: 緑 5 種 / 黄緑 / 橙黄 / 枯れ）に縁の棘をアルファで描く
実: 多角形の核果が集まったパイナップル状の集合果（熟すと橙色、未熟は緑）
揺れ: 幹・枝・支柱根は高さの 2 乗で曲がり、葉はロゼットの付け根から先へしなる（okinawa/anim.py）
"""
import math
import random

import numpy as np
from mathutils import Matrix, Vector

from .. import anim
from .. import common as C
from .. import geo
from ..common import pbr_material, srgb

PREVIEW = dict(cam_dir=(0.45, -1.0, 0.3), lens=38)

# エンジンの頂点アニメーションの種類（glTF のマテリアルの extras に書く。置くだけで風に揺れる）
VERTEX_ANIMATION = "plant"

N_COL = 8        # 葉アトラスの列数
LEAF_L = 1.35    # 葉の標準長(m)
LEAF_W = 0.095   # 葉カードの幅(m, 棘込み)
UP = Vector((0, 0, 1))


# ---------------------------------------------------------------------------
# 共通の補助（hibiscus からも使う）
# ---------------------------------------------------------------------------
def white(nb, w):
    n = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="1D")
    nb.link(w, n.inputs["W"])
    return n.outputs["Value"]


def perp(d):
    """d に垂直な単位ベクトル（水平寄り）"""
    a = d.cross(UP)
    if a.length < 1e-4:
        a = d.cross(Vector((1, 0, 0)))
    return a.normalized()


def rotate_toward(d, axis_perp, ang):
    return (d * math.cos(ang) + axis_perp * math.sin(ang)).normalized()


def register_cutout_aliases(variants):
    """フレームワークの回避策: bake_part は AO ベイク時にアルファ付きパーツを隠すが、
    判定が _REGISTRY の元マテリアル名で行われるため、先にベイク済みで最終マテリアル
    （"<Variant>_<Mat>"）に差し替わった葉パーツが隠れず、幹の AO が真っ黒になる。
    最終マテリアル名もカットアウトとして登録しておく"""
    for vname, objs in variants.items():
        for o in objs:
            for m in o.data.materials:
                info = C._REGISTRY.get(m.name) if m else None
                if info and info["out"].get("alpha") is not None:
                    C._REGISTRY.setdefault(f"{vname}_{m.name.split('.')[0]}", {"out": {"alpha": 1.0}})
    return variants


class MeshAcc:
    """多数の小さな部品を 1 メッシュにまとめる（Proc UV 付き）"""

    def __init__(self):
        self.verts, self.faces, self.uvs = [], [], []
        self.anim = {}  # 開始頂点番号 → [(R, G, B, A), ...]（揺れデータ。okinawa/anim.py）

    def set_anim(self, start, values):
        """verts[start:] に揺れデータを割り当てる"""
        self.anim[start] = list(values)

    def add(self, verts, faces, uvs):
        o = len(self.verts)
        self.verts += [tuple(v) for v in verts]
        self.faces += [tuple(i + o for i in f) for f in faces]
        self.uvs += uvs

    def grid(self, rows, uv_rows):
        """rows[i][j] = 頂点、uv_rows[i][j] = UV の格子を四角形で張る"""
        n, m = len(rows), len(rows[0])
        o = len(self.verts)
        for r in rows:
            self.verts += [tuple(v) for v in r]
        for i in range(n - 1):
            for j in range(m - 1):
                a = o + i * m + j
                self.faces.append((a, a + 1, a + m + 1, a + m))
                self.uvs.append([uv_rows[i][j], uv_rows[i][j + 1], uv_rows[i + 1][j + 1], uv_rows[i + 1][j]])

    def build(self, name):
        if not self.faces:
            return None
        obj = C.mesh_object(name, self.verts, self.faces, self.uvs)
        if self.anim:
            data = np.zeros((len(self.verts), 4))
            done = np.zeros(len(self.verts), dtype=bool)
            for start, vals in self.anim.items():
                data[start:start + len(vals)] = vals
                done[start:start + len(vals)] = True
            if not done.all():
                print(f"[warn] {name}: 揺れデータの無い頂点 {int((~done).sum())} 個")
            anim.write(obj, *data.T)
        return obj


class BarkPacker:
    """複数のチューブ（幹・枝・根）の Bake UV を 1 枚のテクスチャに詰める

    geo.tube_along は各チューブの Bake UV を 0..1 全体に作るため、そのまま結合すると重なる。
    ここでは周長×長さの実寸比の短冊に切ってシェルフ詰めする。
    Proc UV の U には部位番号 kind*2 を足す（シェーダーで樹皮 / 根 / 果柄を塗り分ける）
    """

    def __init__(self, chunk_len=0.55):
        self.items = []
        self.chunk_len = chunk_len

    def tube(self, pts, radii, sides, name, kind=0, v_from_end=False, cap_start=False, cap_end=True,
             ring_fn=None):
        pts = [Vector(p) for p in pts]
        obj = geo.tube_along(pts, radii, sides=sides, name=name, cap_start=cap_start, cap_end=cap_end,
                             ring_fn=ring_fn)
        lens = [0.0]
        for i in range(1, len(pts)):
            lens.append(lens[-1] + (pts[i] - pts[i - 1]).length)
        if kind or v_from_end:
            layer = obj.data.uv_layers["Proc"]
            nq = (len(pts) - 1) * sides
            for p in obj.data.polygons:
                for li in p.loop_indices:
                    u, v = layer.data[li].uv
                    if v_from_end and p.index < nq:
                        v = lens[-1] - v
                    layer.data[li].uv = (u + kind * 2.0, v)
        self.items.append(dict(obj=obj, lens=lens, radii=list(radii), sides=sides,
                               caps=(cap_start, cap_end)))
        return obj

    def _rects(self):
        rects = []  # (w, h, item, kind, data)
        for it in self.items:
            lens, radii = it["lens"], it["radii"]
            n = len(lens)
            i0 = 0
            chunks = []
            for i in range(1, n):
                if lens[i] - lens[i0] > self.chunk_len and i - 1 > i0:
                    chunks.append((i0, i - 1))
                    i0 = i - 1
            chunks.append((i0, n - 1))
            it["chunks"] = chunks
            for ci, (a, b) in enumerate(chunks):
                circ = max(radii[a:b + 1]) * math.tau * 1.03
                rects.append([circ, lens[b] - lens[a], it, "chunk", ci])
            for k, on in enumerate(it["caps"]):
                if on:
                    r = radii[0] if k == 0 else radii[-1]
                    rects.append([2 * r, 2 * r, it, "cap", k])
        return rects

    @staticmethod
    def _shelf(rects, s, margin):
        order = sorted(range(len(rects)), key=lambda k: -rects[k][1])
        pos = [None] * len(rects)
        x = y = margin
        shelf_h = 0.0
        for k in order:
            w, h = rects[k][0] * s, rects[k][1] * s
            if x + w + margin > 1.0:
                x = margin
                y += shelf_h + margin
                shelf_h = 0.0
            if x + w + margin > 1.0 or y + h + margin > 1.0:
                return None
            pos[k] = (x, y)
            x += w + margin
            shelf_h = max(shelf_h, h)
        return pos

    def pack(self, margin=0.006):
        rects = self._rects()
        area = sum(w * h for w, h, *_ in rects)
        lo, hi = 0.0, 2.0 / math.sqrt(area)
        best = None
        for _ in range(28):
            mid = (lo + hi) / 2
            p = self._shelf(rects, mid, margin)
            if p:
                lo, best = mid, (mid, p)
            else:
                hi = mid
        s, pos = best
        place = {}
        for (w, h, it, kind, data), p in zip(rects, pos):
            place[(id(it), kind, data)] = (p, w, h)
        for it in self.items:
            obj, lens, sides = it["obj"], it["lens"], it["sides"]
            n = len(lens)
            layer = obj.data.uv_layers["Bake"]
            face_chunk = {}
            for ci, (a, b) in enumerate(it["chunks"]):
                for i in range(a, b):
                    face_chunk[i] = ci
            nq = (n - 1) * sides
            caps = [k for k, on in enumerate(it["caps"]) if on]
            for poly in obj.data.polygons:
                f = poly.index
                lis = list(poly.loop_indices)
                if f < nq:
                    i, j = divmod(f, sides)
                    ci = face_chunk[i]
                    (x0, y0), w, h = place[(id(it), "chunk", ci)]
                    a = it["chunks"][ci][0]
                    for li, (jj, ii) in zip(lis, [(j, i), (j + 1, i), (j + 1, i + 1), (j, i + 1)]):
                        layer.data[li].uv = (x0 + jj / sides * w * s, y0 + (lens[ii] - lens[a]) * s)
                else:
                    k = caps[f - nq]
                    (x0, y0), w, h = place[(id(it), "cap", k)]
                    js = list(reversed(range(sides))) if k == 0 else list(range(sides))
                    for li, jj in zip(lis, js):
                        ang = jj / sides * math.tau
                        layer.data[li].uv = (x0 + w * s * (0.5 + 0.5 * math.cos(ang)),
                                             y0 + h * s * (0.5 + 0.5 * math.sin(ang)))
        print(f"  [bark pack] {len(rects)} rects, {s:.0f} uv/m (≈{s * 1024:.0f} px/m @1024)")
        return [it["obj"] for it in self.items]


# ---------------------------------------------------------------------------
# マテリアル
# ---------------------------------------------------------------------------
def leaf_material():
    """帯状の葉のアトラス。U: 列(8)×幅, V: 付け根 0 → 先端 1"""

    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        colf = nb.math("FLOOR", nb.mul(U, N_COL))
        x = nb.math("FRACT", nb.mul(U, N_COL))
        s = nb.mul(nb.math("ABSOLUTE", nb.sub(x, 0.5)), 2.0)  # 0=中肋, 1=カード端
        side = nb.math("GREATER_THAN", x, 0.5)
        rnd = white(nb, colf)
        dry = nb.math("GREATER_THAN", colf, 6.5)
        old = nb.math("GREATER_THAN", colf, 4.5)
        # 葉身の半幅（先端で鋭く尖る、付け根の葉鞘はやや細い）
        tip = nb.pow(nb.math("MINIMUM", nb.math("DIVIDE", nb.sub(1.0, V), 0.09), 1.0), 0.85)
        bw = nb.mul(nb.mul(0.72, tip), nb.maprange(V, 0.0, 0.05, 0.85, 1.0))
        # 枯れ葉は縁がぼろぼろ
        tatter = nb.noise(nb.comb(nb.mul(side, 7.3), nb.mul(V, 30.0), colf), 1.0, 4, 0.6)
        bw = nb.mul(bw, nb.sub(1.0, nb.mul(dry, nb.mul(nb.smooth(tatter, 0.45, 0.72), 0.45))))
        # 縁の棘: 先端側へ傾いた小さな三角
        t = nb.math("FRACT", nb.add(nb.mul(V, 110.0), nb.mul(side, 0.5)))
        tri = nb.math("MAXIMUM", nb.math("MINIMUM", nb.math("DIVIDE", nb.sub(t, 0.4), 0.48),
                                          nb.math("DIVIDE", nb.sub(1.0, t), 0.12)), 0.0)
        prot = nb.mul(nb.mul(tri, nb.mul(0.15, tip)), nb.smooth(V, 0.05, 0.12))
        prot = nb.mul(prot, nb.sub(1.0, nb.mul(dry, 0.5)))
        edge = nb.add(bw, prot)
        alpha = nb.smooth(s, nb.add(edge, 0.015), nb.sub(edge, 0.015))
        spine = nb.smooth(s, nb.add(bw, 0.0), nb.add(bw, 0.03))
        q = nb.math("DIVIDE", s, nb.math("MAXIMUM", bw, 0.01))  # 葉身内の相対位置 0..1

        # --- 色 ---
        fac = nb.math("DIVIDE", nb.add(colf, 0.5), N_COL)
        base = nb.ramp(fac, [(0 / 8, srgb("#4a7a2e")), (1 / 8, srgb("#3f6f2b")), (2 / 8, srgb("#548232")),
                             (3 / 8, srgb("#3b6829")), (4 / 8, srgb("#5a8a38")), (5 / 8, srgb("#8d9a3c")),
                             (6 / 8, srgb("#c79a3e")), (7 / 8, srgb("#8e7050"))], interp="CONSTANT")
        # 列内の大きなムラ（黄化は先端から）
        mott = nb.noise(nb.comb(nb.mul(x, 2.0), nb.mul(V, 9.0), nb.mul(colf, 3.1)), 1.0, 4, 0.55)
        col = nb.hsv(base, 0.5, 1.0, nb.maprange(mott, 0.3, 0.7, 0.9, 1.1))
        yell = nb.mul(old, nb.smooth(V, 0.2, 0.8))
        col = nb.mix(col, srgb("#b89a3a"), nb.mul(yell, nb.mul(nb.math("LESS_THAN", colf, 5.5), 0.5)))
        # 縦の細い葉脈
        stripes = nb.math("SINE", nb.mul(s, 95.0))
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(stripes, -1.0, 1.0, 0.94, 1.03))
        # M 字の襞（中肋の両側）は明るく
        pleat = nb.smooth(nb.math("ABSOLUTE", nb.sub(q, 0.42)), 0.07, 0.0)
        col = nb.hsv(col, 0.5, 1.0, nb.add(1.0, nb.mul(pleat, 0.1)))
        # 中肋と縁は黄緑
        midrib = nb.smooth(s, 0.07, 0.025)
        col = nb.mix(col, srgb("#b3b85e"), nb.mul(midrib, nb.mixf(0.65, 0.2, dry)))
        margin = nb.smooth(q, 0.82, 0.98)
        col = nb.mix(col, srgb("#a9b24e"), nb.mul(margin, nb.mixf(0.45, 0.0, old)))
        # 付け根の葉鞘は白っぽい
        col = nb.mix(col, srgb("#cfd0a6"), nb.smooth(V, 0.07, 0.015))
        # 緑の葉の先端枯れ
        tipburn = nb.mul(nb.smooth(V, nb.maprange(rnd, 0, 1, 0.9, 0.97), 0.995),
                         nb.math("GREATER_THAN", rnd, 0.3))
        col = nb.mix(col, srgb("#7c5a33"), tipburn)
        # 枯れ葉の縦筋
        fib = nb.noise(nb.comb(nb.mul(s, 40.0), nb.mul(V, 3.0), colf), 1.0, 3, 0.6)
        col = nb.mix(col, srgb("#6a5238"), nb.mul(dry, nb.smooth(fib, 0.5, 0.7)))
        # 棘は付け根が淡緑、先が赤褐色
        spcol = nb.mix(srgb("#b8c07a"), srgb("#7a4526"), nb.smooth(s, nb.add(bw, 0.03), edge))
        col = nb.mix(col, spcol, spine)

        rough = nb.mixf(0.4, 0.78, dry)
        rough = nb.mixf(rough, 0.6, spine)
        height = nb.add(nb.mul(stripes, 0.15), nb.mul(pleat, 0.6))
        height = nb.add(height, nb.mul(midrib, 0.5))
        cavity = nb.maprange(nb.smooth(q, 0.3, 0.0), 0, 1, 1.0, 0.85)
        return dict(color=col, rough=rough, alpha=alpha, height=height, height_scale=0.0015, cavity=cavity)

    return pbr_material("AdanLeaf", fn, res=2048, uv="atlas", double_sided=True,
                        atlas_size=(LEAF_W * N_COL, LEAF_L))


def bark_material():
    """幹・支柱根・果柄。Proc U の整数部 0=幹, 2=根, 4=果柄。V は長さ(m)"""

    def fn(nb):
        uv = nb.sep(nb.uv("Proc"))
        U, V = uv[0], uv[1]
        kind = nb.math("FLOOR", nb.mul(U, 0.5))
        root = nb.math("GREATER_THAN", kind, 0.5)
        stalk = nb.math("GREATER_THAN", kind, 1.5)
        root = nb.sub(root, stalk)
        u = nb.math("FRACT", U)
        a = nb.mul(u, math.tau)
        R = 0.09
        cyl = nb.comb(nb.mul(nb.math("COSINE", a), R), nb.mul(nb.math("SINE", a), R), V)
        cyl = nb.vmath("ADD", cyl, nb.comb(nb.mul(kind, 13.7), 0.0, 0.0))
        z = nb.sep(nb.coord("Object"))[2]
        big = nb.noise(cyl, 2.0, 4, 0.6)
        mid = nb.noise(cyl, 9.0, 5, 0.6)
        # 葉痕のリング（幹は 3〜4cm 間隔、ゆらぎ付き）
        warp = nb.noise(cyl, 3.0, 3, 0.5)
        rf = nb.math("FRACT", nb.add(nb.mul(V, 28.0), nb.mul(warp, 1.2)))
        ring_line = nb.smooth(nb.math("ABSOLUTE", nb.sub(rf, 0.5)), 0.5, 0.42)
        ring_ridge = nb.smooth(nb.math("ABSOLUTE", nb.sub(rf, 0.38)), 0.14, 0.0)
        ring_str = nb.smooth(nb.noise(cyl, 5.0, 3, 0.5), 0.3, 0.55)
        ring_line = nb.mul(ring_line, ring_str)
        # イボ状の突起（根に多い）
        wart_d = nb.voronoi(nb.mapping(cyl, scale=(1, 1, 1)), 38.0, rand=1.0)
        wart_amt = nb.mixf(0.25, 1.0, root)
        wart = nb.mul(nb.smooth(wart_d, nb.mixf(0.12, 0.2, root), 0.0), wart_amt)
        # 縦の細い割れ
        crack = nb.noise(nb.mapping(cyl, scale=(14, 14, 0.8)), 3.0, 5, 0.6)
        crack_line = nb.smooth(nb.math("ABSOLUTE", nb.sub(crack, 0.5)), 0.035, 0.0)

        bark = nb.ramp(big, [(0.3, srgb("#5f574c")), (0.55, srgb("#7a7163")), (0.78, srgb("#948b7b"))])
        lichen = nb.smooth(nb.noise(cyl, 7.0, 4, 0.6), 0.6, 0.72)
        bark = nb.mix(bark, srgb("#b7b2a2"), nb.mul(lichen, 0.55))
        bark = nb.mix(bark, srgb("#3f372f"), nb.mul(ring_line, 0.75))
        bark = nb.mix(bark, srgb("#9c9383"), nb.mul(ring_ridge, nb.mul(ring_str, 0.3)))
        rootc = nb.ramp(big, [(0.3, srgb("#6f6150")), (0.6, srgb("#8a7a64")), (0.8, srgb("#a08e74"))])
        rootc = nb.mix(rootc, srgb("#c4ae84"), nb.mul(nb.smooth(V, 0.25, 0.04), 0.9))  # 根冠（先端）
        rootc = nb.mix(rootc, srgb("#4b4035"), nb.mul(ring_line, 0.3))
        stalkc = nb.mix(srgb("#8c8a4c"), srgb("#a79f62"), mid)
        stalkc = nb.mix(stalkc, srgb("#5e5a36"), nb.mul(ring_line, 0.6))
        col = nb.mix(bark, rootc, root)
        col = nb.mix(col, stalkc, stalk)
        col = nb.mix(col, srgb("#4a4036"), nb.mul(crack_line, 0.5))
        col = nb.mix(col, srgb("#5a4a3a"), nb.mul(wart, 0.45))
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(mid, 0.3, 0.7, 0.9, 1.08))
        # 地面際は砂で白っぽく
        col = nb.mix(col, srgb("#bcae90"), nb.mul(nb.smooth(z, 0.18, -0.02), 0.65))

        height = nb.add(nb.mul(ring_line, -0.7), nb.mul(ring_ridge, nb.mul(ring_str, 0.35)))
        height = nb.add(height, nb.mul(wart, 0.9))
        height = nb.add(height, nb.mul(crack_line, -0.5))
        height = nb.add(height, nb.mul(mid, 0.25))
        rough = nb.maprange(mid, 0.3, 0.7, 0.78, 0.92)
        cavity = nb.maprange(nb.add(nb.mul(ring_line, 0.7), nb.mul(crack_line, 0.3)), 0, 1, 1.0, 0.75)
        return dict(color=col, rough=rough, height=height, height_scale=0.006, cavity=cavity)

    return pbr_material("AdanBark", fn, res=1024, ao_distance=0.35, uv="keep")


def fruit_material():
    """集合果。属性 Fruit: R=核果内の位置(0 中心 → 1 境目), G=核果ごとの乱数, B=緯度, A=熟度"""

    def fn(nb):
        rad = nb.attr("Fruit", "Color")
        rgb = nb.node("ShaderNodeSeparateColor")
        nb.link(rad, rgb.inputs[0])
        r, g, b = rgb.outputs[0], rgb.outputs[1], rgb.outputs[2]
        ripe0 = nb.attr("Fruit", "Alpha")
        co = nb.coord("Object")
        # 核果ごとの熟度（下側・外側から色づく）
        ripe = nb.add(ripe0, nb.add(nb.mul(nb.sub(0.55, b), 0.5), nb.mul(nb.sub(g, 0.5), 0.45)))
        ripe = nb.math("MINIMUM", nb.math("MAXIMUM", ripe, 0.0), 1.0)
        fine = nb.noise(co, 120.0, 3, 0.6)
        speck = nb.smooth(nb.noise(co, 60.0, 2, 0.5), 0.62, 0.7)
        # 熟した実: 面は橙、境目は黄、頂点（柱頭の痕）は暗褐
        face_r = nb.mix(srgb("#e2742a"), srgb("#d55a22"), nb.smooth(g, 0.6, 0.95))
        edge_r = srgb("#e9b347")
        face_u = nb.mix(srgb("#5d8a2e"), srgb("#6e9636"), g)
        edge_u = srgb("#b9c06e")
        face = nb.mix(face_u, face_r, nb.smooth(ripe, 0.25, 0.75))
        edge = nb.mix(edge_u, edge_r, nb.smooth(ripe, 0.2, 0.7))
        col = nb.mix(face, edge, nb.smooth(r, 0.45, 0.95))
        col = nb.mix(col, srgb("#8a7a38"), nb.smooth(r, 0.93, 1.0))  # 深い溝
        apex = nb.smooth(r, 0.12, 0.03)
        col = nb.mix(col, nb.mix(srgb("#34461c"), srgb("#5a3a1a"), ripe), nb.mul(apex, 0.9))
        col = nb.mix(col, srgb("#3a3a20"), nb.mul(speck, 0.25))
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(fine, 0.3, 0.7, 0.93, 1.05))
        height = nb.add(nb.mul(fine, 0.3), nb.mul(apex, 0.6))
        rough = nb.add(nb.maprange(r, 0.3, 1.0, 0.38, 0.6), nb.mul(fine, 0.1))
        cavity = nb.maprange(r, 0.8, 1.0, 1.0, 0.6)
        return dict(color=col, rough=rough, height=height, height_scale=0.0015, cavity=cavity)

    return pbr_material("AdanFruit", fn, res=1024, ao_distance=0.15, ao_samples=48)


# ---------------------------------------------------------------------------
# 形状
# ---------------------------------------------------------------------------
def _leaf(acc, base, d0, length, col, rnd, droop, fold=34.0, twist=0.0, kink=None, segs=7, width=1.0,
          wind=None):
    """V 字断面の帯状の葉を acc に追加。重力で先へいくほど垂れる。wind = dict(G, A, b, r)"""
    ds = length / segs
    pts = [Vector(base)]
    d = Vector(d0).normalized()
    for i in range(segs):
        t = i / segs
        g = droop * (0.45 + 1.0 * t) * ds
        d = (d + Vector((0, 0, -g))).normalized()
        if kink and abs(t - kink[0]) < 0.5 / segs:
            # 途中で折れ曲がる古い葉
            ax = perp(d)
            d = (Matrix.Rotation(kink[1], 3, ax) @ d).normalized()
            if d.z > 0.2:
                d = (Matrix.Rotation(-2 * kink[1], 3, ax) @ d).normalized()
        q = pts[-1] + d * ds
        if q.z < 0.03:
            # 砂に届いた葉は地面に沿って寝かせる
            d = Vector((d.x, d.y, max(d.z, 0.0)))
            d = d.normalized() if d.length > 1e-3 else Vector((1, 0, 0))
            q = pts[-1] + d * ds
            q.z = max(q.z, 0.03)
        pts.append(q)
    horiz = Vector((d0.x, d0.y, 0.0))
    if horiz.length < 1e-3:
        horiz = Vector((1, 0, 0))
    side0 = Vector((-horiz.y, horiz.x, 0.0)).normalized()
    fa = math.radians(fold)
    rows, uvs = [], []
    u0 = col / N_COL
    xs = (0.012, 0.5, 0.988)
    n = len(pts)
    for i, p in enumerate(pts):
        y = i / (n - 1)
        tng = (pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]).normalized()
        sd = (side0 - tng * side0.dot(tng)).normalized()
        a = twist * y
        sd = (sd * math.cos(a) + tng.cross(sd) * math.sin(a)).normalized()
        upn = tng.cross(sd).normalized()
        # 幅: 付け根は細く、中ほどは一定、先 45% で細くなる
        w = LEAF_W * 0.5 * width * min(1.0, 0.45 + y * 7.0) * (1.0 - 0.8 * max(0.0, y - 0.55) / 0.45)
        f = fa * (1.0 - 0.5 * y)
        row = []
        for xk in (-1, 0, 1):
            row.append(p + sd * (xk * w * math.cos(f)) + upn * (abs(xk) * w * math.sin(f)))
        rows.append(row)
        uvs.append([(u0 + xx / N_COL, y) for xx in xs])
    if wind:
        # 先ほど・縁ほど震え、付け根からの 2 乗でしなる
        acc.set_anim(len(acc.verts), [(wind["r"] * (0.4 + 0.6 * abs(xk)) * (i / (n - 1)) ** 1.2, wind["G"],
                                       wind["b"] * (i / (n - 1)) ** 2, wind["A"])
                                      for i in range(n) for xk in (-1, 0, 1)])
    acc.grid(rows, uvs)


def _rosette(acc, tip, axis, rnd, n_leaves=22, n_dry=3, scale=1.0, fruit_side=None, wind=None):
    """枝先のロゼット。若い葉は上向きの束、外側ほど開いて垂れる。
    wind = dict(key, A) — 葉はすべて枝先の曲げ A を持ち、葉ごとに位相を変える"""

    def leaf_wind(tag, k, L, b, r):
        if not wind:
            return None
        return dict(G=anim.hash01(wind["key"], tag, k), A=wind["A"], b=b * L, r=r)

    axis = axis.normalized()
    golden = math.radians(137.5)
    p0 = perp(axis)
    p1 = axis.cross(p0)
    az0 = rnd.uniform(0, math.tau)
    for k in range(n_leaves):
        age = k / max(1, n_leaves - 1)
        az = az0 + k * golden + rnd.uniform(-0.15, 0.15)
        radial = p0 * math.cos(az) + p1 * math.sin(az)
        theta = math.radians(8 + 112 * age ** 0.7 + rnd.uniform(-8, 8))
        d = (axis * math.cos(theta) + radial * math.sin(theta)).normalized()
        base = tip + axis * 0.03 - axis * (age * 0.17 * scale) + radial * 0.02
        L = LEAF_L * scale * rnd.uniform(0.8, 1.05) * (0.7 + 0.3 * min(1.0, age * 3.0))
        droop = 0.3 + 2.1 * age ** 1.3 + rnd.uniform(-0.2, 0.4)
        if age > 0.85:
            col = rnd.choice([5, 5, 6]) if rnd.random() < 0.45 else rnd.randrange(0, 5)
        else:
            col = rnd.randrange(0, 5)
        kink = (rnd.uniform(0.35, 0.6), math.radians(rnd.uniform(15, 35))) if age > 0.6 and rnd.random() < 0.35 \
            else None
        _leaf(acc, base, d, L, col, rnd, droop, twist=rnd.uniform(-0.8, 0.8), kink=kink,
              wind=leaf_wind("leaf", k, L, 0.1, 0.02))
    # 枯れて垂れ下がった葉（スカート状）
    for k in range(n_dry):
        az = rnd.uniform(0, math.tau)
        radial = p0 * math.cos(az) + p1 * math.sin(az)
        d = (radial * 0.55 - UP * 0.85 + axis * 0.1).normalized()
        base = tip - axis * (0.2 * scale + rnd.uniform(0, 0.08)) + radial * 0.03
        L = LEAF_L * scale * rnd.uniform(0.7, 0.95)
        _leaf(acc, base, d, L, rnd.choice([7, 7, 6]), rnd,
              droop=0.4, fold=55.0, twist=rnd.uniform(-1.5, 1.5), width=0.8, wind=leaf_wind("dry", k, L, 0.05, 0.005))


def _fruit(name, center, radius, height, ripeness, rnd, n_cells=48):
    """集合果。球面ボロノイの各セルを盛り上げた多角形の核果にする"""
    from scipy.spatial import SphericalVoronoi
    # フィボナッチ球 + ゆらぎ → ロイド緩和
    i = np.arange(n_cells) + 0.5
    phi = np.arccos(1 - 2 * i / n_cells)
    th = math.pi * (1 + 5 ** 0.5) * i
    P = np.stack([np.cos(th) * np.sin(phi), np.sin(th) * np.sin(phi), np.cos(phi)], -1)
    P += np.random.RandomState(rnd.randrange(1 << 30)).normal(0, 0.08, P.shape)
    P /= np.linalg.norm(P, axis=1, keepdims=True)
    for _ in range(4):
        sv = SphericalVoronoi(P)
        sv.sort_vertices_of_regions()
        P = np.array([sv.vertices[r].mean(0) for r in sv.regions])
        P /= np.linalg.norm(P, axis=1, keepdims=True)
    sv = SphericalVoronoi(P)
    sv.sort_vertices_of_regions()

    def shape(v, rscale):
        # 楕円体（上端＝果柄側はやや平ら）
        v = Vector(v).normalized()
        z = v.z
        r = radius * rscale * (1.0 - 0.06 * max(0.0, z) ** 3)
        return Vector((v.x * r, v.y * r, z * height * 0.5 * rscale))

    verts = [shape(v, 0.9) for v in sv.vertices]
    faces, data = [], []
    for ci, reg in enumerate(sv.regions):
        c = Vector(P[ci])
        m = len(reg)
        cr = rnd.random()
        lat = (c.z + 1) / 2
        dome = rnd.uniform(0.98, 1.03)
        inner = []
        for vi in reg:
            q = (Vector(sv.vertices[vi]) * 0.7 + c * 0.3).normalized()
            verts.append(shape(q, dome))
            inner.append(len(verts) - 1)
        verts.append(shape(c, dome * 1.035))
        apex = len(verts) - 1
        for k in range(m):
            a, b = reg[k], reg[(k + 1) % m]
            qa, qb = inner[k], inner[(k + 1) % m]
            faces.append((a, b, qb, qa))
            data.append([(1.0, cr, lat), (1.0, cr, lat), (0.55, cr, lat), (0.55, cr, lat)])
            faces.append((qa, qb, apex))
            data.append([(0.55, cr, lat), (0.55, cr, lat), (0.0, cr, lat)])
    # 面の向きを外向きに揃える
    for fi, f in enumerate(faces):
        a, b, c3 = verts[f[0]], verts[f[1]], verts[f[2]]
        nrm = (b - a).cross(c3 - a)
        if nrm.dot(a + b + c3) < 0:
            faces[fi] = tuple(reversed(f))
            data[fi] = list(reversed(data[fi]))
    obj = C.mesh_object(name, [v + center for v in verts], faces)
    me = obj.data
    attr = me.color_attributes.new("Fruit", "FLOAT_COLOR", "CORNER")
    for poly in me.polygons:
        for li, dd in zip(poly.loop_indices, data[poly.index]):
            attr.data[li].color = (dd[0], dd[1], dd[2], ripeness)
    C.set_smooth(obj, True)
    return obj


def _trunk_ring(i, j, a):
    return 1.0 + 0.04 * math.sin(i * 1.9 + j * 0.7) + 0.03 * math.sin(a * 2 + i * 0.5)


def _grow(bp, rnd, start, d, length, r0, r1, depth, spec, tips, name, trunks, top):
    """二又分枝する枝を再帰的に作る"""
    end_dir = (d + UP * spec["photo"]).normalized()
    wob = Vector((rnd.uniform(-1, 1), rnd.uniform(-1, 1), 0)) * 0.06 * length
    p0 = Vector(start)
    p1 = p0 + d * length * 0.35
    p2 = p1 + (d * 0.4 + end_dir * 0.6).normalized() * length * 0.3 + wob
    p3 = p2 + end_dir * length * 0.35
    n = max(4, int(length / 0.1) + 1)
    pts = geo.bezier_points(p0, p1, p2, p3, n)
    radii = [r0 + (r1 - r0) * (i / (n - 1)) ** 0.8 for i in range(n)]
    if depth == top:
        # 主幹の根元は支柱根の下で細くなる
        radii = [r * (0.72 + 0.28 * min(1.0, (p.z + 0.2) / 0.9)) for r, p in zip(radii, pts)]
        trunks.append((pts, radii))
    sides = 10 if r0 > 0.07 else (8 if r0 > 0.04 else 7)
    if depth == 0:
        # 枝先は葉の付け根に隠れるよう細める
        radii[-1] *= 0.55
        radii[-2] *= 0.85
    bp.tube(pts, radii, sides, name, kind=0, cap_end=True, ring_fn=_trunk_ring)
    tdir = (pts[-1] - pts[-2]).normalized()
    if depth == 0:
        tips.append((pts[-1], tdir, r1))
        return
    nchild = 3 if rnd.random() < spec.get("p3", 0.15) else 2
    az = rnd.uniform(0, math.tau)
    pa = perp(tdir)
    pb = tdir.cross(pa)
    for c in range(nchild):
        a = az + c * math.tau / nchild + rnd.uniform(-0.4, 0.4)
        radial = pa * math.cos(a) + pb * math.sin(a)
        ang = math.radians(rnd.uniform(*spec["fork"]))
        cd = rotate_toward(tdir, radial, ang)
        L = length * rnd.uniform(*spec["ratio"])
        cr0 = r1 * 0.95
        _grow(bp, rnd, pts[-1] - tdir * r1 * 1.2, cd, L, cr0, cr0 * spec["taper"], depth - 1, spec, tips,
              f"{name}_{c}", trunks, top)


def _prop_roots(bp, rnd, pts, radii, n, hmin, hmax, name, az0=0.0, aerial=1):
    """幹の下部から斜めに砂へ刺さる支柱根"""
    out = []
    zs = [p.z for p in pts]
    for k in range(n + aerial):
        is_aerial = k >= n
        h = rnd.uniform(hmin, hmax) if not is_aerial else rnd.uniform(hmax * 0.8, hmax * 1.25)
        i = next((ii for ii, z in enumerate(zs) if z >= h), len(zs) - 1)
        P = pts[i]
        az = az0 + k * math.tau / max(1, n) + rnd.uniform(-0.35, 0.35)
        o = Vector((math.cos(az), math.sin(az), 0.0))
        start = P + o * radii[i] * 0.3
        if is_aerial:
            L = rnd.uniform(0.2, 0.45)
            end = start + o * L * 0.35 - UP * L
            p1 = start + o * 0.06 - UP * 0.03
            p2 = end + UP * L * 0.4
            r_a, r_b = 0.016, 0.014
        else:
            spread = math.tan(math.radians(rnd.uniform(22, 38))) * (h + 0.1) + radii[i]
            end = Vector((P.x + o.x * spread, P.y + o.y * spread, -0.22))
            p1 = start + o * 0.12 - UP * 0.04
            p2 = end + (start - end) * 0.4 + o * 0.04
            r_a, r_b = rnd.uniform(0.022, 0.03), rnd.uniform(0.032, 0.045)
        m = max(5, int((end - start).length / 0.12) + 1)
        rp = geo.bezier_points(start, p1, p2, end, m)
        rr = [r_a + (r_b - r_a) * (j / (m - 1)) for j in range(m)]
        if is_aerial:
            rr[-1] *= 0.6

        def ring(i2, j2, a2, _s=rnd.random() * 10):
            return 1.0 + 0.06 * math.sin(i2 * 2.3 + _s + j2 * 1.3)

        out.append(bp.tube(rp, rr, 7, f"{name}_root{k}", kind=1, v_from_end=True, cap_start=False,
                           cap_end=is_aerial, ring_fn=ring))
    return out


def adan(prefix, seed, trunks_spec, spec, n_leaves, fruits, root_n, root_h, leaf_scale=1.0):
    rnd = random.Random(seed)
    bp = BarkPacker()
    tips, trunks = [], []
    for ti, (start, d, L, r0, depth) in enumerate(trunks_spec):
        _grow(bp, rnd, Vector(start), Vector(d).normalized(), L, r0, r0 * spec["taper"], depth, spec,
              tips, f"{prefix}_br{ti}", trunks, top=depth)
    for ti, (pts, radii) in enumerate(trunks):
        _prop_roots(bp, rnd, pts, radii, root_n[ti], root_h[0], root_h[1], f"{prefix}_t{ti}",
                    az0=rnd.uniform(0, math.tau))
    # 揺れ: 一番高い枝先で main_amp。葉・実は枝先の値を引き継ぐ
    top_z = max(t[0].z for t in tips)
    main_amp = 0.03 * top_z

    def bend_at(z):
        return float(anim.main_bend(z, top_z, main_amp))

    acc = MeshAcc()
    for k, (tip, d, r) in enumerate(tips):
        _rosette(acc, tip, d, rnd, n_leaves=n_leaves + rnd.randrange(-2, 3), n_dry=rnd.choice([2, 3, 3, 4]),
                 scale=leaf_scale * rnd.uniform(0.9, 1.05), wind=dict(key=(prefix, k), A=bend_at(tip.z)))
    parts = []
    # 実: ロゼットの中から太い果柄で垂れ下がる
    fmat = fruit_material()
    order = sorted(range(len(tips)), key=lambda k: -tips[k][0].z)
    for fi, ripeness in enumerate(fruits):
        tip, d, r = tips[order[(fi * 2 + 1) % len(tips)]]
        az = rnd.uniform(0, math.tau)
        o = Vector((math.cos(az), math.sin(az), 0))
        s0 = tip - d * 0.08
        s3 = s0 + o * 0.28 - UP * 0.42
        sp = geo.bezier_points(s0, s0 + d * 0.08 + o * 0.08, s3 + UP * 0.12, s3, 6)
        stalk = bp.tube(sp, [0.022, 0.02, 0.019, 0.018, 0.018, 0.018], 6, f"{prefix}_stalk{fi}", kind=2)
        h = rnd.uniform(0.19, 0.22)
        fr = _fruit(f"{prefix}_fruit{fi}", s3 - UP * (h * 0.5 - 0.01), h * 0.43, h, ripeness, rnd)
        fr.rotation_euler = (rnd.uniform(-0.2, 0.2), rnd.uniform(-0.2, 0.2), 0)
        C.assign(fr, fmat)
        # 実は果柄の先で振り子のように揺れる（果柄の付け根 0 → 実で 0.04 m）
        g = anim.hash01(prefix, "fruit", fi)
        s = anim.nearest_param(stalk, sp)
        anim.write(stalk, 0.0, g, 0.04 * s * s, bend_at(tip.z))
        anim.write(fr, 0.0, g, 0.04, bend_at(tip.z))
        print(f"  fruit {fi}: {tuple(round(x, 2) for x in s3)}")
        parts.append(fr)
    bmat = bark_material()
    for o in bp.pack():
        C.set_smooth(o, True)
        C.assign(o, bmat)
        if not anim.has(o):
            # 幹・枝・支柱根（砂に刺さる先は z < 0 で動かない）
            anim.write(o, 0.0, anim.hash01(o.name), 0.0, anim.main_bend(anim.world_co(o)[:, 2], top_z, main_amp))
        parts.append(o)
    leaves = acc.build(f"{prefix}_leaves")
    C.assign(leaves, leaf_material())
    parts.append(leaves)
    zmax = max(v[2] for v in acc.verts)
    print(f"  {prefix}: {len(tips)} tips, leaf top {zmax:.2f} m")
    return parts


def build():
    spec_a = dict(photo=0.35, fork=(22, 38), ratio=(0.6, 0.78), taper=0.78, p3=0.15)
    spec_b = dict(photo=0.3, fork=(28, 45), ratio=(0.7, 0.85), taper=0.78, p3=0.25)
    return register_cutout_aliases({
        "Adan_A": adan("AdanA", 11,
                       [((0, 0, -0.25), (0.35, 0.1, 1.0), 1.55, 0.1, 2),
                        ((0.12, -0.1, -0.25), (-0.6, -0.35, 1.0), 1.25, 0.085, 1)],
                       spec_a, n_leaves=29, fruits=[1.0, 0.55, 0.0], root_n=[6, 5], root_h=(0.3, 1.0)),
        "Adan_B": adan("AdanB", 5,
                       [((0, 0, -0.2), (0.1, 0.15, 1.0), 0.75, 0.075, 2)],
                       spec_b, n_leaves=27, fruits=[0.85], root_n=[5], root_h=(0.15, 0.5), leaf_scale=0.9),
    })
