"""Build data/earth.bin from real elevation: ETOPO5 (NOAA, 5 arc-minutes,
tools/etopo5.dat, 4320 x 2160 big-endian int16 metres, row 0 at 90 N,
column 0 at 0 E going east). The hand-drawn template (make_earth.py)
made every range a tube and every plateau a polygon; the real thing is
asymmetric, branched and gappy, and no amount of drawing gets there.

Output: "EARTHTPL", int32 w, int32 h, then h*w int16 metres, row 0 at -90
latitude, column 0 at -180 longitude, cell centres -- the same layout the
drawn template had, at three times the resolution.
"""
import struct
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SRC = HERE / "etopo5.dat"
W, H = 4320, 2160

def main():
    raw = np.fromfile(SRC, dtype=">i2")
    assert raw.size == W * H, raw.size
    e = raw.reshape(H, W).astype(np.int16)
    # ETOPO5's row 0 is 90 N and its column 0 is 0 E: flip to south-up rows
    # and roll so column 0 is 180 W. Its cells are at the grid lines, ours
    # at centres; a quarter of a cell is below what anything here resolves.
    e = e[::-1, :]
    e = np.roll(e, W // 2, axis=1)
    out = np.clip(e, -32000, 32000).astype("<i2")
    path = ROOT / "data" / "earth.bin"
    with open(path, "wb") as f:
        f.write(b"EARTHTPL")
        f.write(struct.pack("<ii", W, H))
        f.write(out.tobytes())
    land = (out > 0).mean()
    print("wrote", path, out.shape, "land fraction %.3f" % land, "max", out.max(), "min", out.min())

    # The lakes: Natural Earth's 1:110m lake polygons (the same scale as the
    # coastlines), as a mask at the template's resolution. A 19 km flood on
    # 9 km data dams every gorge -- the Congo, the Danube, the Yangtze -- so
    # the flood's lakes are not believed on the template; these are the
    # world's shape, like its coasts. data/earth_lakes.bin: "EARTHLAK",
    # int32 w, int32 h, then h*w bytes, 1 inside a lake, same layout.
    import sys
    sys.path.insert(0, str(HERE))
    import make_earth as drawn
    drawn.W, drawn.H = W, H
    lakes = drawn.rasterise(HERE / "ne_110m_lakes.geojson")
    lpath = ROOT / "data" / "earth_lakes.bin"
    with open(lpath, "wb") as f:
        f.write(b"EARTHLAK")
        f.write(struct.pack("<ii", W, H))
        f.write(lakes.astype(np.uint8).tobytes())
    print("wrote", lpath, "lake cells", int(lakes.sum()))

    img = np.zeros((H, W, 3), dtype=np.uint8)
    ef = out.astype(np.float64)
    sea = ef <= 0
    img[sea] = np.stack([np.full(sea.sum(), 20), np.full(sea.sum(), 50), np.clip(130 + ef[sea] / 40, 40, 130)], 1).astype(np.uint8)
    t = np.clip(ef / 4000.0, 0, 1)
    img[~sea, 0] = (60 + 180 * t[~sea]).astype(np.uint8)
    img[~sea, 1] = (120 + 100 * t[~sea]).astype(np.uint8)
    img[~sea, 2] = (50 + 200 * t[~sea]).astype(np.uint8)
    img = img[::-1]
    with open(HERE / "earth_preview.ppm", "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (W, H))
        f.write(img.tobytes())

if __name__ == "__main__":
    main()
