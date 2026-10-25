"""共通のプロシージャル材質"""
from .common import pbr_material, srgb


def limestone(name="Limestone", res=2048, tide_top=1.7, tide_bottom=-0.2, scale=1.0, dark_top=0.35):
    """琉球石灰岩。多孔質で明るい灰ベージュ、潮間帯は黒っぽく濡れた色

    tide_top/bottom: 潮間帯（藻・フジツボで黒ずむ帯）の高さ(m)
    """

    def fn(nb):
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        cs = nb.mapping(co, scale=(scale, scale, scale))
        # 大きなムラ
        big = nb.noise(cs, 0.7, 5, 0.6)
        mid = nb.noise(cs, 3.5, 6, 0.62)
        # 多孔質の穴（ボロノイ F1 の小さい所が穴）
        pits_a = nb.voronoi(cs, 9.0, rand=1.0)
        pits_b = nb.voronoi(nb.mapping(cs, loc=(3.1, 1.7, 0.4)), 23.0, rand=1.0)
        pit_a = nb.smooth(pits_a, 0.16, 0.0)
        pit_b = nb.smooth(pits_b, 0.18, 0.0)
        pits = nb.math("MAXIMUM", nb.mul(pit_a, 0.9), nb.mul(pit_b, 0.6))
        # 鋭いカレン（尖った溶食面）
        ridge = nb.noise(nb.mapping(cs, scale=(1, 1, 2.2)), 6.0, 8, 0.7, distortion=0.4)
        fine = nb.noise(cs, 60.0, 4, 0.6)

        base = nb.ramp(big, [(0.30, srgb("#9a8f78")), (0.55, srgb("#bdb299")), (0.75, srgb("#d3c9b0"))])
        grey = nb.mix(base, srgb("#8e8a80"), nb.smooth(mid, 0.45, 0.7))
        # 黒い地衣類の斑点
        lichen = nb.smooth(nb.noise(cs, 11.0, 4, 0.6), 0.62, 0.7)
        grey = nb.mix(grey, srgb("#3b3a35"), nb.mul(lichen, dark_top))
        # 潮間帯（上端をノイズで乱す）
        edge = nb.add(z, nb.mul(nb.sub(big, 0.5), 0.9))
        tide = nb.mul(nb.smooth(edge, tide_top, tide_top - 0.5), nb.smooth(edge, tide_bottom - 0.3, tide_bottom))
        algae = nb.mix(srgb("#3f3d33"), srgb("#5a5838"), nb.smooth(fine, 0.4, 0.7))
        col = nb.mix(grey, algae, nb.mul(tide, 0.9))
        # 穴の奥は暗く
        col = nb.mix(col, srgb("#4d493f"), nb.mul(pits, 0.75))
        # 砂がたまった地面際
        sand = nb.smooth(z, 0.25, -0.05)
        col = nb.mix(col, srgb("#d8cdb0"), nb.mul(sand, 0.6))

        rough = nb.maprange(nb.add(nb.mul(tide, -0.35), nb.mul(fine, 0.1)), -0.35, 0.1, 0.55, 0.95)
        height = nb.add(nb.add(nb.mul(pits, -0.6), nb.mul(ridge, 0.5)), nb.mul(fine, 0.15))
        cavity = nb.maprange(pits, 0.0, 1.0, 1.0, 0.55)
        return dict(color=col, rough=rough, height=height, height_scale=0.035, cavity=cavity)

    return pbr_material(name, fn, res=res, ao_samples=64)


def concrete(name="Concrete", res=1024, tint="#a8a59c", scale=1.0, stain_top=None, salt=0.3, ao_distance=0.8,
             uv="smart"):
    """打ち放しのコンクリート。気泡穴・雨だれの筋・砂ぼこり

    stain_top: この高さ(m)より下を潮と砂で黒ずませる（None なら汚さない）
    """

    def fn(nb):
        co = nb.coord("Object")
        z = nb.sep(co)[2]
        cs = nb.mapping(co, scale=(scale, scale, scale))
        big = nb.noise(cs, 0.8, 4, 0.55)
        mid = nb.noise(cs, 4.5, 5, 0.6)
        fine = nb.noise(cs, 40.0, 3, 0.6)
        base = nb.ramp(big, [(0.3, srgb("#8f8c84")), (0.55, srgb(tint)), (0.75, srgb("#b7b4ab"))])
        col = nb.hsv(base, 0.5, 1.0, nb.maprange(mid, 0.3, 0.7, 0.92, 1.05))
        # 雨だれの縦筋
        streak = nb.noise(nb.mapping(cs, scale=(16.0, 16.0, 0.8)), 1.2, 4, 0.55)
        col = nb.mix(col, srgb("#6f6c65"), nb.mul(nb.smooth(streak, 0.56, 0.72), 0.35))
        # 気泡穴
        bh = nb.voronoi(cs, 38.0)
        bhr = nb.sep(nb.voronoi(cs, 38.0, out="Color"))[0]
        holes = nb.mul(nb.smooth(bh, 0.16, 0.06), nb.math("GREATER_THAN", bhr, 0.82))
        col = nb.mix(col, srgb("#55544f"), nb.mul(holes, 0.8))
        # 白い塩の析出
        sl = nb.smooth(nb.noise(cs, 7.0, 4, 0.6), 0.62, 0.72)
        col = nb.mix(col, srgb("#d4d1c8"), nb.mul(sl, salt))
        height = nb.add(nb.add(nb.mul(holes, -1.0), nb.mul(fine, 0.25)), nb.mul(mid, 0.2))
        rough = nb.maprange(fine, 0.3, 0.7, 0.84, 0.95)
        if stain_top is not None:
            zz = nb.add(z, nb.mul(nb.sub(big, 0.5), 0.35))
            stain = nb.smooth(zz, stain_top, stain_top - 0.5)
            col = nb.mix(col, nb.mix(srgb("#6d6a60"), srgb("#5d5c53"), mid), nb.mul(stain, 0.55))
        return dict(color=col, rough=rough, height=height, height_scale=0.003,
                    cavity=nb.maprange(holes, 0, 1, 1.0, 0.5))

    return pbr_material(name, fn, res=res, ao_distance=ao_distance, uv=uv)


def painted_metal(name, color, res=512, chip=0.25, rust="#7a4020", gloss=0.35, ao_distance=0.4, uv="smart"):
    """塗装した鋼板・パイプ。日焼けで白っぽく褪せ、角から塗装がはげて錆が出る"""

    def fn(nb):
        co = nb.coord("Object")
        n = nb.noise(co, 9.0, 6, 0.65)
        fade = nb.noise(co, 1.5, 3, 0.5)
        paint = nb.mix(srgb(color), nb.hsv(srgb(color), 0.5, 0.75, 1.12), nb.smooth(fade, 0.4, 0.7))
        # 雨だれの筋
        streak = nb.noise(nb.mapping(co, scale=(18.0, 18.0, 0.9)), 1.4, 4, 0.55)
        paint = nb.mix(paint, nb.hsv(paint, 0.5, 1.0, 0.82), nb.mul(nb.smooth(streak, 0.58, 0.72), 0.5))
        chips = nb.smooth(n, 0.72 - chip * 0.1, 0.74 - chip * 0.1)
        chips = nb.mul(chips, chip * 4.0)
        chips = nb.math("MINIMUM", chips, 1.0)
        rcol = nb.mix(srgb("#4d2a18"), srgb(rust), nb.noise(co, 50.0, 3, 0.6))
        col = nb.mix(paint, rcol, chips)
        rough = nb.mixf(gloss, 0.85, chips)
        height = nb.add(nb.mul(nb.sub(1.0, chips), 0.4), nb.mul(nb.noise(co, 80.0, 3, 0.6), 0.1))
        return dict(color=col, rough=rough, height=height, height_scale=0.001)

    return pbr_material(name, fn, res=res, ao_distance=ao_distance, uv=uv)


def paving(name, res=2048, block=(0.2, 0.1), bands=(), x0=0.0, uv="smart"):
    """インターロッキングブロックの舗装（上面が z=0、長手が X の馬目地）

    block: ブロックの (長さ, 幅) m。bands: [(y0, y1, "#色"), ...] の範囲だけ別の色のブロックにする
    x0: 目地の起点の X（モジュールの端をブロックの境に合わせる）
    """
    bw, bh = block

    def fn(nb):
        co = nb.coord("Object")
        X, Y, Z = nb.sep(co)
        row = nb.math("FLOOR", nb.math("DIVIDE", Y, bh))
        u = nb.add(nb.math("DIVIDE", nb.sub(X, x0), bw), nb.mul(nb.math("FLOORED_MODULO", row, 2.0), 0.5))
        coln = nb.math("FLOOR", u)
        fx = nb.math("FRACT", u)
        fy = nb.math("FRACT", nb.math("DIVIDE", Y, bh))
        dx = nb.mul(nb.math("MINIMUM", fx, nb.sub(1.0, fx)), bw)
        dy = nb.mul(nb.math("MINIMUM", fy, nb.sub(1.0, fy)), bh)
        edge = nb.math("MINIMUM", dx, dy)
        joint = nb.smooth(edge, 0.004, 0.0015)
        wn = nb.node("ShaderNodeTexWhiteNoise", noise_dimensions="3D")
        nb.link(nb.comb(coln, row, 0.0), wn.inputs["Vector"])
        h1 = wn.outputs["Value"]
        h2 = nb.bw(wn.outputs["Color"])
        col = nb.ramp(h1, [(0.0, srgb("#8e4a36")), (0.35, srgb("#a65a42")), (0.7, srgb("#b86d50")),
                           (1.0, srgb("#c88566"))])
        for y0, y1, c in bands:
            m = nb.mul(nb.math("GREATER_THAN", Y, y0), nb.math("LESS_THAN", Y, y1))
            col = nb.mix(col, nb.hsv(srgb(c), 0.5, 1.0, nb.maprange(h1, 0, 1, 0.9, 1.08)), m)
        # ブロックごとのわずかな傾きの明暗と、日焼けのムラ
        col = nb.hsv(col, 0.5, 1.0, nb.maprange(h2, 0, 1, 0.92, 1.06))
        fade = nb.noise(co, 0.6, 4, 0.55)
        col = nb.mix(col, nb.hsv(col, 0.5, 0.7, 1.12), nb.mul(nb.smooth(fade, 0.45, 0.75), 0.6))
        # 黒い汚れの点（ガム・油）と砂ぼこり
        spot = nb.smooth(nb.voronoi(co, 6.0), 0.05, 0.02)
        spot = nb.mul(spot, nb.math("GREATER_THAN", nb.sep(nb.voronoi(co, 6.0, out="Color"))[0], 0.85))
        col = nb.mix(col, srgb("#3d3530"), nb.mul(spot, 0.5))
        dust = nb.smooth(nb.noise(co, 3.0, 4, 0.6), 0.5, 0.7)
        col = nb.mix(col, srgb("#cdbf9f"), nb.mul(dust, 0.3))
        col = nb.mix(col, srgb("#bfb193"), joint)
        grain = nb.noise(co, 90.0, 3, 0.6)
        rough = nb.mixf(nb.maprange(grain, 0.3, 0.7, 0.78, 0.9), 0.95, joint)
        height = nb.add(nb.smooth(edge, 0.0, 0.01), nb.mul(grain, 0.15))
        return dict(color=col, rough=rough, height=height, height_scale=0.004,
                    cavity=nb.maprange(joint, 0, 1, 1.0, 0.6))

    return pbr_material(name, fn, res=res, ao_distance=0.3, uv=uv)


def galvanized(name="Galvanized", res=512, white_rust=0.6, uv="smart"):
    """溶融亜鉛めっき鋼（スパングル模様・白錆）"""

    def fn(nb):
        oc = nb.coord("Object")
        sp = nb.bw(nb.voronoi(oc, 45.0, out="Color"))
        col = nb.mix(srgb("#8e9291"), srgb("#b4b8b6"), sp)
        n = nb.noise(oc, 9.0, 6, 0.65)
        white_r = nb.smooth(n, 0.55, 0.7)
        col = nb.mix(col, srgb("#c9cbc4"), nb.mul(white_r, white_rust))
        metal = nb.sub(1.0, nb.mul(white_r, 0.6 * white_rust))
        rough = nb.mixf(0.38, 0.75, nb.mul(white_r, white_rust))
        return dict(color=col, rough=rough, metal=metal, height=nb.noise(oc, 80.0, 3, 0.6), height_scale=0.0005)

    return pbr_material(name, fn, res=res, ao_distance=0.2, uv=uv)
