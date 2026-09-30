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
