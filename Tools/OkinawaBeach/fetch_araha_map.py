"""アラハビーチ（北谷町）の浜・海岸線・公園・歩道の線を OpenStreetMap から取り、シーンの座標の JSON にする

使い方: python Tools/OkinawaBeach/fetch_araha_map.py
出力: Tools/OkinawaBeach/data/araha_map.json（地形 araha_terrain.py とシーンの書き出しが読む）

座標は浜の中央を原点にした平面（x = 東 [m]、y = 北 [m]）。エンジンへは Blender と同じく (x, z, y) で対応する。
地図データは (c) OpenStreetMap contributors（ODbL 1.0）。
"""
import json
import math
import os
import urllib.parse
import urllib.request

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(TOOL_DIR, "data", "araha_map.json")
BBOX = (26.300, 127.748, 26.322, 127.766)   # 南, 西, 北, 東
LAT0, LON0 = 26.3115, 127.7575
KX = 111320.0 * math.cos(math.radians(LAT0))
KY = 110540.0
# シーンの原点（浜の中央）の、LAT0/LON0 からのずれ [m]
OX, OY = 80.0, -780.0


def query():
    b = ",".join(str(v) for v in BBOX)
    q = (f"[out:json][timeout:60];(way({b})[\"natural\"];way({b})[\"leisure\"];"
         f"way({b})[\"highway\"~\"footway|pedestrian|path\"];);out geom;")
    req = urllib.request.Request("https://overpass-api.de/api/interpreter",
                                 data=urllib.parse.urlencode({"data": q}).encode(),
                                 headers={"User-Agent": "CoreEngine-OkinawaBeach"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.load(r)


def local(p):
    return ((p["lon"] - LON0) * KX - OX, (p["lat"] - LAT0) * KY - OY)


def main():
    data = query()
    out = {"attribution": "Map data (c) OpenStreetMap contributors, ODbL 1.0 (https://www.openstreetmap.org/copyright)",
           "origin": {"lat": LAT0 + OY / KY, "lon": LON0 + OX / KX},
           "axes": "x = east [m], y = north [m]", "coastline": [], "beach": None, "park": None, "footways": []}
    for e in data["elements"]:
        g = e.get("geometry")
        if not g:
            continue
        t = e.get("tags", {})
        pts = [local(p) for p in g]
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        if max(xs) < -600 or min(xs) > 450 or max(ys) < -700 or min(ys) > 500:
            continue
        r = [[round(x, 2), round(y, 2)] for x, y in pts]
        if t.get("natural") == "coastline":
            out["coastline"].append({"id": e["id"], "points": r})
        elif t.get("natural") == "beach" and min(xs) > -150 and max(xs) < 200 and min(ys) > -400:
            out["beach"] = {"id": e["id"], "points": r}
        elif t.get("leisure") == "park" and "安良波" in t.get("name", ""):
            out["park"] = {"id": e["id"], "points": r}
        elif t.get("highway") in ("footway", "pedestrian", "path") and max(xs) > -200 and min(xs) < 300 \
                and max(ys) > -450 and min(ys) < 450:
            out["footways"].append({"id": e["id"], "points": r})
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    print(f"coastline {len(out['coastline'])} / beach {out['beach'] is not None} / park {out['park'] is not None} "
          f"/ footways {len(out['footways'])} -> {OUT}")


if __name__ == "__main__":
    main()
