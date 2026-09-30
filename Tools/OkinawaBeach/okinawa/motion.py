"""揺れ・泳ぎのジオメトリノード（Blender 上の確認用。エンジンの頂点シェーダーと同じ式）

glTF を読み込んだメッシュの UV マップ UVMap.001 (= TEXCOORD_1), UVMap.002 (= TEXCOORD_2) を読み、
Scene Time で頂点を動かす。README の HLSL の例と同じ計算をしている。

植物（OkiWind）: TEXCOORD_1 = (R 震え m, G 位相), TEXCOORD_2 = (B しなり m, A 全体の曲げ m)
魚（OkiFishSwim）: TEXCOORD_1 = (t 吻端 0 → 尾 1, 横揺れ m), TEXCOORD_2 = (胸びれ m, 左右 ±1)

周波数は Loop 秒の整数倍にそろえてあるので、Loop 秒ごとに同じ動きに戻る（GIF をつなぎ目なくループできる）。
"""
import math

import bpy

UV_A = "UVMap.001"
UV_B = "UVMap.002"


class GB:
    """ジオメトリノードの式を組み立てる小さな補助"""

    def __init__(self, name):
        old = bpy.data.node_groups.get(name)
        if old is not None:
            bpy.data.node_groups.remove(old)
        ng = bpy.data.node_groups.new(name, "GeometryNodeTree")
        ng.interface.new_socket("Geometry", in_out="INPUT", socket_type="NodeSocketGeometry")
        ng.interface.new_socket("Geometry", in_out="OUTPUT", socket_type="NodeSocketGeometry")
        self.ng, self.nodes, self.links = ng, ng.nodes, ng.links
        self.gin = self.nodes.new("NodeGroupInput")
        self.gout = self.nodes.new("NodeGroupOutput")
        self._time = None

    def inp(self, name, default=0.0, typ="NodeSocketFloat"):
        s = self.ng.interface.new_socket(name, in_out="INPUT", socket_type=typ)
        s.default_value = default
        return self.gin.outputs[name]

    def _set(self, sock, val):
        if isinstance(val, bpy.types.NodeSocket):
            self.links.new(val, sock)
        else:
            sock.default_value = val

    def m(self, op, a, b=0.0, c=0.0):
        n = self.nodes.new("ShaderNodeMath")
        n.operation = op
        for s, v in zip(n.inputs, (a, b, c)):
            self._set(s, v)
        return n.outputs[0]

    def add(self, *xs):
        out = xs[0]
        for x in xs[1:]:
            out = self.m("ADD", out, x)
        return out

    def mul(self, *xs):
        out = xs[0]
        for x in xs[1:]:
            out = self.m("MULTIPLY", out, x)
        return out

    def sin(self, a):
        return self.m("SINE", a)

    def cos(self, a):
        return self.m("COSINE", a)

    def v(self, op, a, b=(0.0, 0.0, 0.0), scale=None):
        n = self.nodes.new("ShaderNodeVectorMath")
        n.operation = op
        self._set(n.inputs[0], a)
        self._set(n.inputs[1], b)
        if scale is not None:
            self._set(n.inputs[3], scale)
        return n.outputs[1] if op in ("LENGTH", "DOT_PRODUCT", "DISTANCE") else n.outputs[0]

    def vadd(self, *xs):
        out = xs[0]
        for x in xs[1:]:
            out = self.v("ADD", out, x)
        return out

    def scale(self, vec, s):
        return self.v("SCALE", vec, scale=s)

    def sep(self, vec):
        n = self.nodes.new("ShaderNodeSeparateXYZ")
        self._set(n.inputs[0], vec)
        return n.outputs

    def comb(self, x, y, z):
        n = self.nodes.new("ShaderNodeCombineXYZ")
        for s, val in zip(n.inputs, (x, y, z)):
            self._set(s, val)
        return n.outputs[0]

    def attr(self, name):
        n = self.nodes.new("GeometryNodeInputNamedAttribute")
        n.data_type = "FLOAT_VECTOR"
        n.inputs["Name"].default_value = name
        return n.outputs["Attribute"]

    def pos(self):
        return self.nodes.new("GeometryNodeInputPosition").outputs[0]

    def normal(self):
        return self.nodes.new("GeometryNodeInputNormal").outputs[0]

    def time(self):
        if self._time is None:
            self._time = self.nodes.new("GeometryNodeInputSceneTime").outputs["Seconds"]
        return self._time

    def finish(self, position):
        sp = self.nodes.new("GeometryNodeSetPosition")
        self.links.new(self.gin.outputs["Geometry"], sp.inputs["Geometry"])
        self.links.new(position, sp.inputs["Position"])
        self.links.new(sp.outputs["Geometry"], self.gout.inputs["Geometry"])
        # 見やすいように左から右へ並べる
        for i, n in enumerate(self.nodes):
            n.location = ((i % 12) * 180, -(i // 12) * 160)
        return self.ng


def wind_group():
    """植物の揺れ（Crysis 方式を簡略化）。オブジェクト空間（Z-up、原点 = 根元）で計算する

    1. 全体の曲げ: 風下へ gust、横へ side だけ A 倍して動かし、原点からの距離を保つ（幹が伸びない）
    2. 葉・枝のしなり: 上下（BranchUp）と風下（Bias + 揺れ）へ B 倍。位相は葉ごとの G
    3. 震え: 法線方向（表裏で向きをそろえる）へ R 倍の速い揺れ
    """
    g = GB("OkiWind")
    wind = g.inp("Wind Dir", (1.0, 0.0, 0.0), "NodeSocketVector")
    S = g.inp("Strength", 1.0)
    ph = g.inp("Phase", 0.0)
    loop = g.inp("Loop", 4.0)
    k_main = g.inp("Main Scale", 1.0)
    k_branch = g.inp("Branch Scale", 1.0)
    k_flutter = g.inp("Flutter Scale", 1.0)
    up_k = g.inp("Branch Up", 0.8)
    bias = g.inp("Bias", 0.6)
    # しなり・震えの周波数（基本周波数 1 / Loop の倍数）。海草は波の寄せ返しに合わせて遅くする
    f_b1 = g.inp("Branch Freq", 4.0)
    f_b2 = g.inp("Branch Freq 2", 9.0)
    f_fl = g.inp("Flutter Freq", 28.0)
    t = g.time()
    w0 = g.m("DIVIDE", 2 * math.pi, loop)
    wt = g.mul(w0, t)                                   # 基本の角振動数 x 時間

    def wave(n, phase):
        return g.sin(g.add(g.mul(wt, n), phase))

    a, b = g.sep(g.attr(UV_A)), g.sep(g.attr(UV_B))
    R, G, B, A = a[0], a[1], b[0], b[1]
    D = g.v("NORMALIZE", wind)
    Dp = g.v("CROSS_PRODUCT", (0.0, 0.0, 1.0), D)
    # 1. 全体の曲げ
    gust = g.mul(S, g.add(0.55, g.mul(0.3, wave(1.0, ph)), g.mul(0.15, wave(3.0, g.mul(ph, 1.3)))))
    side = g.mul(S, 0.25, wave(2.0, g.mul(ph, 2.0)))
    off_main = g.scale(g.vadd(g.scale(D, gust), g.scale(Dp, side)), g.mul(A, k_main))
    p = g.pos()
    L = g.v("LENGTH", p)
    p1 = g.scale(g.v("NORMALIZE", g.vadd(p, off_main)), L)
    # 2. 葉・枝のしなり
    phb = g.add(g.mul(G, 2 * math.pi), ph)
    wb = g.add(g.mul(0.65, wave(f_b1, phb)), g.mul(0.35, wave(f_b2, g.mul(phb, 1.7))))
    dir_b = g.vadd(g.scale((0.0, 0.0, 1.0), g.mul(wb, up_k)), g.scale(D, g.add(g.mul(S, bias), g.mul(0.4, wb))))
    off_branch = g.scale(dir_b, g.mul(B, S, k_branch))
    # 3. 震え（表裏の面で法線の向きをそろえる: z >= 0 の側へ）
    nrm = g.normal()
    sgn = g.m("SUBTRACT", g.mul(g.m("GREATER_THAN", g.sep(nrm)[2], -1e-6), 2.0), 1.0)
    phf = g.add(g.mul(G, 10 * math.pi), g.mul(g.v("DOT_PRODUCT", p, (1.3, 1.7, 2.1)), 3.0))
    off_fl = g.scale(nrm, g.mul(sgn, R, S, k_flutter, wave(f_fl, phf)))
    return g.finish(g.vadd(p1, off_branch, off_fl))


def swim_group():
    """魚の泳ぎ（オブジェクト空間: 頭 +X、背 +Z、左 +Y）

    体: 横（Y）へ amp * sin(2π(t / λ - BodyHz * time + Phase))（頭から尾へ進む波）
    胸びれ: 横へ side * pec * sin(2π(FinHz * time + 1.7 Phase))、前後へ少し漕ぐ"""
    g = GB("OkiFishSwim")
    ph = g.inp("Phase", 0.0)
    body_hz = g.inp("Body Hz", 2.0)
    fin_hz = g.inp("Fin Hz", 4.0)
    lam = g.inp("Wavelength", 0.95)
    k = g.inp("Amplitude", 1.0)
    t = g.time()
    a, b = g.sep(g.attr(UV_A)), g.sep(g.attr(UV_B))
    tb, amp, pec, side = a[0], a[1], b[0], b[1]
    arg = g.mul(2 * math.pi, g.add(g.m("DIVIDE", tb, lam), g.mul(-1.0, body_hz, t), ph))
    flap = g.mul(2 * math.pi, g.add(g.mul(fin_hz, t), g.mul(ph, 1.7)))
    oy = g.add(g.mul(amp, k, g.sin(arg)), g.mul(side, pec, g.sin(flap)))
    ox = g.mul(-0.5, pec, g.cos(flap))
    return g.finish(g.vadd(g.pos(), g.comb(ox, oy, 0.0)))


def add_modifier(obj, group, **inputs):
    """obj に GN モディファイアを付け、入力の値を設定する（名前は group の入力名）。
    リンク複製（obj.copy()）で元のモディファイアごと複製された場合に二重にならないよう、同じグループのものは外す"""
    for m in [m for m in obj.modifiers if m.type == "NODES" and m.node_group == group]:
        obj.modifiers.remove(m)
    mod = obj.modifiers.new(group.name, "NODES")
    mod.node_group = group
    for item in group.interface.items_tree:
        if item.item_type != "SOCKET" or item.in_out != "INPUT" or item.name not in inputs:
            continue
        val = inputs[item.name]
        mod[item.identifier] = tuple(val) if isinstance(val, (tuple, list)) else val
    return mod


def loop_hz(hz, loop):
    """Loop 秒でちょうど整数回になるよう周波数を丸める"""
    return max(1, round(hz * loop)) / loop
