"""アラハビーチのまわりの街（建物・道路・土地の使い方・海岸線）と標高を取り、シーンの座標のデータにする

使い方: python Tools/OkinawaBeach/fetch_araha_town.py
出力:
  Tools/OkinawaBeach/data/araha_town.json  OpenStreetMap の建物・道路・面・海岸線（build_araha_town.py が読む）
  Tools/OkinawaBeach/data/araha_dem.npz    国土地理院の標高タイル（DEM10B）を 10 m 格子へ取り直した標高

座標は fetch_araha_map.py と同じ（浜の中央が原点、x = 東 [m]、y = 北 [m]）。
地図データは (c) OpenStreetMap contributors（ODbL 1.0）。標高は国土地理院の標高タイルを加工したもの。
"""
import io
import json
import math
import os
import urllib.parse
import urllib.request

import numpy as np

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_TOWN = os.path.join(TOOL_DIR, "data", "araha_town.json")
OUT_DEM = os.path.join(TOOL_DIR, "data", "araha_dem.npz")
BBOX = (26.272, 127.735, 26.345, 127.792)   # 南, 西, 北, 東
LAT0, LON0 = 26.3115, 127.7575
KX = 111320.0 * math.cos(math.radians(LAT0))
KY = 110540.0
OX, OY = 80.0, -780.0
DEM_ZOOM = 14
DEM_STEP = 10.0
DEM_BOX = (-3000.0, 5600.0, -4200.0, 5400.0)   # x0, x1, y0, y1
AREA_KINDS = {
    ("amenity", "parking"): "parking", ("landuse", "grass"): "grass", ("leisure", "park"): "park",
    ("leisure", "garden"): "grass", ("leisure", "pitch"): "pitch", ("leisure", "golf_course"): "golf",
    ("leisure", "stadium"): "pitch", ("leisure", "track"): "track", ("natural", "wood"): "wood",
    ("landuse", "forest"): "wood", ("natural", "scrub"): "scrub", ("natural", "grassland"): "grass",
    ("natural", "beach"): "beach", ("natural", "sand"): "beach", ("natural", "water"): "water",
    ("landuse", "military"): "military", ("landuse", "residential"): "residential",
    ("landuse", "commercial"): "commercial", ("landuse", "retail"): "commercial",
    ("landuse", "industrial"): "industrial", ("landuse", "cemetery"): "cemetery", ("amenity", "school"): "school",
    ("landuse", "farmland"): "farmland", ("landuse", "meadow"): "grass", ("landuse", "recreation_ground"): "grass",
    ("golf", "fairway"): "golf", ("golf", "green"): "golf", ("leisure", "playground"): "grass",
}


def local(p):
    return ((p["lon"] - LON0) * KX - OX, (p["lat"] - LAT0) * KY - OY)


def overpass(q):
    req = urllib.request.Request("https://overpass-api.de/api/interpreter",
                                 data=urllib.parse.urlencode({"data": q}).encode(),
                                 headers={"User-Agent": "CoreEngine-OkinawaBeach"})
    with urllib.request.urlopen(req, timeout=300) as r:
        return json.load(r)


def rounded(pts):
    return [[round(x, 1), round(y, 1)] for x, y in pts]


def number(s):
    try:
        return float(str(s).split(";")[0].replace("m", "").strip())
    except ValueError:
        return None


def outer_rings(e):
    """マルチポリゴンの外側の輪（つながった線をつないだもの）"""
    segs = [[local(p) for p in m["geometry"]] for m in e.get("members", [])
            if m.get("role") == "outer" and m.get("geometry")]
    rings = []
    while segs:
        ring = segs.pop(0)
        changed = True
        while changed and math.dist(ring[0], ring[-1]) > 0.01:
            changed = False
            for i, s in enumerate(segs):
                if math.dist(ring[-1], s[0]) < 0.01:
                    ring += s[1:]
                elif math.dist(ring[-1], s[-1]) < 0.01:
                    ring += s[::-1][1:]
                else:
                    continue
                segs.pop(i)
                changed = True
                break
        if len(ring) >= 4 and math.dist(ring[0], ring[-1]) < 0.01:
            rings.append(ring[:-1])
    return rings


def fetch_osm():
    b = ",".join(str(v) for v in BBOX)
    q = (f"[out:json][timeout:240];("
         f"way({b})[\"building\"];relation({b})[\"building\"][\"type\"=\"multipolygon\"];"
         f"way({b})[\"highway\"];"
         f"way({b})[\"landuse\"];way({b})[\"leisure\"];way({b})[\"natural\"];way({b})[\"amenity\"~\"parking|school\"];"
         f"way({b})[\"golf\"];relation({b})[\"landuse\"][\"type\"=\"multipolygon\"];"
         f"relation({b})[\"leisure\"][\"type\"=\"multipolygon\"];relation({b})[\"natural\"][\"type\"=\"multipolygon\"];"
         f");out geom;")
    data = overpass(q)
    s, w, n, e = BBOX
    x0, y0 = local({"lat": s, "lon": w})
    x1, y1 = local({"lat": n, "lon": e})
    out = {"attribution": "Map data (c) OpenStreetMap contributors, ODbL 1.0 (https://www.openstreetmap.org/copyright)",
           "axes": "x = east [m], y = north [m]", "box": [round(x0, 1), round(x1, 1), round(y0, 1), round(y1, 1)],
           "buildings": [], "roads": [], "areas": [], "coastline": []}
    for e in data["elements"]:
        t = e.get("tags", {})
        if e["type"] == "relation":
            rings = outer_rings(e)
        elif e.get("geometry"):
            pts = [local(p) for p in e["geometry"]]
            closed = math.dist(pts[0], pts[-1]) < 0.01
            rings = [pts[:-1]] if closed and len(pts) >= 4 else []
        else:
            continue
        if "building" in t:
            if t["building"] in ("no", "ruins", "construction") and "building:levels" not in t:
                continue
            for ring in rings:
                b = {"id": e["id"], "pts": rounded(ring), "type": t["building"]}
                for key, name in (("building:levels", "levels"), ("height", "height"), ("min_height", "min_height"),
                                  ("building:min_level", "min_level"), ("roof:levels", "roof_levels")):
                    v = number(t.get(key))
                    if v is not None:
                        b[name] = v
                for key in ("name", "tourism", "shop", "amenity", "building:colour", "roof:shape"):
                    if key in t:
                        b[key.replace("building:", "").replace(":", "_")] = t[key]
                out["buildings"].append(b)
            continue
        if e["type"] == "way" and "highway" in t:
            pts = [local(p) for p in e["geometry"]]
            r = {"id": e["id"], "pts": rounded(pts), "highway": t["highway"]}
            for key in ("lanes", "width"):
                v = number(t.get(key))
                if v is not None:
                    r[key] = v
            for key in ("oneway", "bridge", "tunnel", "layer", "area", "service", "footway"):
                if key in t:
                    r[key] = t[key]
            out["roads"].append(r)
            continue
        if e["type"] == "way" and t.get("natural") == "coastline":
            out["coastline"].append({"id": e["id"], "pts": rounded([local(p) for p in e["geometry"]])})
            continue
        kind = None
        for (k, v), name in AREA_KINDS.items():
            if t.get(k) == v:
                kind = name
                break
        if kind:
            for ring in rings:
                out["areas"].append({"id": e["id"], "pts": rounded(ring), "kind": kind})
    return out


def fetch_dem():
    """標高タイル（テキスト、256x256、海は e）を取り、シーンの座標の 10 m 格子へ最近傍で取り直す。海は nan"""
    n = 2 ** DEM_ZOOM
    x0, x1, y0, y1 = DEM_BOX

    def tile_of(x, y):
        lat = (y + OY) / KY + LAT0
        lon = (x + OX) / KX + LON0
        fx = (lon + 180) / 360 * n
        fy = (1 - np.log(np.tan(np.radians(lat)) + 1 / np.cos(np.radians(lat))) / math.pi) / 2 * n
        return fx, fy

    xs = np.arange(x0, x1 + 0.1, DEM_STEP)
    ys = np.arange(y0, y1 + 0.1, DEM_STEP)
    gx, gy = np.meshgrid(xs, ys)
    fx, fy = tile_of(gx, gy)
    tx = np.floor(fx).astype(int)
    ty = np.floor(fy).astype(int)
    out = np.full(gx.shape, np.nan, np.float32)
    for kx in range(tx.min(), tx.max() + 1):
        for ky in range(ty.min(), ty.max() + 1):
            url = f"https://cyberjapandata.gsi.go.jp/xyz/dem/{DEM_ZOOM}/{kx}/{ky}.txt"
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "CoreEngine-OkinawaBeach"})
                text = urllib.request.urlopen(req, timeout=60).read().decode()
            except urllib.error.HTTPError:
                continue
            rows = [r.split(",") for r in text.strip().split("\n")]
            a = np.array([[float(v) if v != "e" else np.nan for v in r] for r in rows], np.float32)
            m = (tx == kx) & (ty == ky)
            px = np.clip(((fx[m] - kx) * 256).astype(int), 0, 255)
            py = np.clip(((fy[m] - ky) * 256).astype(int), 0, 255)
            out[m] = a[py, px]
    return xs, ys, out


def main():
    town = fetch_osm()
    os.makedirs(os.path.dirname(OUT_TOWN), exist_ok=True)
    with open(OUT_TOWN, "w", encoding="utf-8", newline="\n") as f:
        json.dump(town, f, ensure_ascii=False, separators=(",", ":"))
    print(f"buildings {len(town['buildings'])} / roads {len(town['roads'])} / areas {len(town['areas'])} "
          f"/ coastline {len(town['coastline'])} -> {OUT_TOWN}")
    xs, ys, h = fetch_dem()
    # 0.1 m 単位の整数で持つ（海は -1）
    q = np.where(np.isnan(h), -1, np.round(h * 10)).astype(np.int16)
    buf = io.BytesIO()
    np.savez_compressed(buf, h=q, x0=xs[0], y0=ys[0], step=DEM_STEP)
    with open(OUT_DEM, "wb") as f:
        f.write(buf.getvalue())
    print(f"dem {h.shape} ({np.nanmin(h):.1f}..{np.nanmax(h):.1f} m) -> {OUT_DEM}")


if __name__ == "__main__":
    main()
