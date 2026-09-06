"""Build data/earth.bin: a coarse Earth elevation template for the "earth" seed.

Coastlines are Natural Earth 1:110m land polygons (tools/ne_110m_land.geojson).
Elevation is hand-drawn on top: a gentle rise inland from every coast, plateaus
as filled polygons, ranges as polylines with a width and a crest height, and
ice sheets on Greenland and Antarctica as domes. It does not have to be
accurate; it has to look like Earth, so the climate can be judged against a
map everyone knows.

Output: "EARTHTPL", int32 w, int32 h, then h*w int16 metres, row 0 at -90
latitude, column 0 at -180 longitude, cell centres.
"""
import json, struct, math
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
W, H = 1440, 720  # quarter-degree

# ---------------------------------------------------------------- rasterise land
def rasterise(geojson_path):
    land = np.zeros((H, W), dtype=bool)
    gj = json.loads(Path(geojson_path).read_text(encoding="utf-8"))
    rings = []
    for f in gj["features"]:
        g = f["geometry"]
        polys = g["coordinates"] if g["type"] == "MultiPolygon" else [g["coordinates"]]
        for poly in polys:
            for ring in poly:  # outer and holes alike: even-odd fill
                rings.append(np.array(ring, dtype=np.float64))
    # scanline even-odd per ring, accumulated with xor
    for ring in rings:
        lon, lat = ring[:, 0], ring[:, 1]
        n = len(ring)
        for row in range(H):
            y = (row + 0.5) / H * 180.0 - 90.0
            xs = []
            for i in range(n - 1):
                y0, y1 = lat[i], lat[i + 1]
                if (y0 <= y) == (y1 <= y):
                    continue
                t = (y - y0) / (y1 - y0)
                xs.append(lon[i] + t * (lon[i + 1] - lon[i]))
            if not xs:
                continue
            xs.sort()
            for a, b in zip(xs[0::2], xs[1::2]):
                c0 = int(math.floor((a + 180.0) / 360.0 * W))
                c1 = int(math.floor((b + 180.0) / 360.0 * W))
                c0 = max(c0, 0); c1 = min(c1, W - 1)
                if c1 >= c0:
                    land[row, c0:c1 + 1] ^= True
    return land

# ---------------------------------------------------------------- distances
def distance_km_to(mask):
    """Chamfer distance (km) from every cell to the nearest True cell, lon
    wrapping, lat cells scaled by cos."""
    INF = 1e9
    d = np.where(mask, 0.0, INF)
    dy = 111.0 * 180.0 / H
    lat = (np.arange(H) + 0.5) / H * 180.0 - 90.0
    dx = dy * np.maximum(np.cos(np.radians(lat)), 0.05)  # per row
    for _ in range(2):
        for y in range(H):
            row = d[y]
            if y > 0:
                row = np.minimum(row, d[y - 1] + dy)
            # sweep along the row both ways with wrap: three passes suffice
            for _s in range(2):
                row = np.minimum(row, np.roll(row, 1) + dx[y])
                row = np.minimum(row, np.roll(row, -1) + dx[y])
            d[y] = row
        for y in range(H - 1, -1, -1):
            row = d[y]
            if y < H - 1:
                row = np.minimum(row, d[y + 1] + dy)
            for _s in range(2):
                row = np.minimum(row, np.roll(row, 1) + dx[y])
                row = np.minimum(row, np.roll(row, -1) + dx[y])
            d[y] = row
    # the row sweeps only propagate one cell per pass; iterate until settled
    for _ in range(40):
        before = d.copy()
        for y in range(H):
            row = d[y]
            row = np.minimum(row, np.roll(row, 1) + dx[y])
            row = np.minimum(row, np.roll(row, -1) + dx[y])
            if y > 0: row = np.minimum(row, d[y - 1] + dy)
            if y < H - 1: row = np.minimum(row, d[y + 1] + dy)
            d[y] = row
        if np.array_equal(before, d):
            break
    return d

# ---------------------------------------------------------------- drawing
LON = (np.arange(W) + 0.5) / W * 360.0 - 180.0
LAT = (np.arange(H) + 0.5) / H * 180.0 - 90.0
LONG, LATG = np.meshgrid(LON, LAT)

def km_between(lon0, lat0):
    """Distance in km from every cell to a point (equirectangular, good enough)."""
    dlon = (LONG - lon0 + 180.0) % 360.0 - 180.0
    x = dlon * 111.0 * np.cos(np.radians((LATG + lat0) * 0.5))
    y = (LATG - lat0) * 111.0
    return np.sqrt(x * x + y * y)

def polyline_km(points):
    """Distance in km from every cell to a polyline, segment by segment."""
    best = np.full((H, W), 1e9)
    for (lon0, lat0), (lon1, lat1) in zip(points[:-1], points[1:]):
        steps = max(2, int(math.hypot((lon1 - lon0) * math.cos(math.radians((lat0 + lat1) / 2)),
                                      lat1 - lat0) * 2))
        for k in range(steps + 1):
            t = k / steps
            best = np.minimum(best, km_between(lon0 + t * (lon1 - lon0), lat0 + t * (lat1 - lat0)))
    return best

def polygon_mask(points):
    """Even-odd fill of a lon/lat polygon."""
    m = np.zeros((H, W), dtype=bool)
    pts = points + [points[0]]
    for row in range(H):
        y = LAT[row]
        xs = []
        for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
            if (y0 <= y) == (y1 <= y):
                continue
            xs.append(x0 + (y - y0) / (y1 - y0) * (x1 - x0))
        xs.sort()
        for a, b in zip(xs[0::2], xs[1::2]):
            c0 = max(int(math.floor((a + 180) / 360 * W)), 0)
            c1 = min(int(math.floor((b + 180) / 360 * W)), W - 1)
            if c1 >= c0: m[row, c0:c1 + 1] = True
    return m

# Ranges: crest height (m), half-width (km) at which the range has fallen to
# a third, and the crest line. Sizes are rounded, positions from memory of
# an atlas; the point is a recognisable Earth, not a survey.
RANGES = [
    (6000, 150, [(73, 36), (78, 34), (84, 29), (88, 28), (95, 29), (97, 28)]),      # Himalaya
    (5000, 120, [(68, 35), (72, 36), (76, 36)]),                                     # Hindu Kush / Karakoram
    (4500, 120, [(78, 36), (90, 36), (100, 36)]),                                    # Kunlun
    (4000, 120, [(70, 42), (80, 42), (88, 43)]),                                     # Tien Shan
    (3000, 120, [(85, 48), (92, 50), (98, 52)]),                                     # Altai
    (2500, 100, [(90, 52), (100, 53)]),                                              # Sayan
    (2000, 80, [(120, 56), (130, 56)]),                                              # Stanovoy
    (2000, 100, [(130, 62), (135, 66), (140, 70)]),                                  # Verkhoyansk
    (2500, 100, [(140, 64), (150, 66)]),                                             # Chersky
    (3000, 80, [(158, 53), (160, 57)]),                                              # Kamchatka
    (2500, 60, [(131, 33), (136, 36), (140, 38), (141, 41)]),                        # Japan
    (3000, 120, [(44, 37), (48, 34), (52, 31), (56, 28)]),                           # Zagros
    (3500, 80, [(49, 36), (53, 36), (57, 37)]),                                      # Elburz
    (4000, 80, [(40, 44), (44, 43), (48, 42)]),                                      # Caucasus
    (3500, 100, [(6, 46), (8, 46), (10, 46.5), (13, 47), (15, 47.5)]),               # Alps
    (2800, 60, [(-2, 43), (0, 42.7), (2, 42.5)]),                                    # Pyrenees
    (2000, 80, [(17, 49), (22, 49), (25, 47), (24, 45.5)]),                          # Carpathians
    (1800, 120, [(6, 60), (8, 62), (12, 65), (16, 68), (20, 70)]),                   # Scandes
    (1500, 80, [(58, 52), (59, 57), (60, 62), (64, 67)]),                            # Urals
    (3500, 100, [(-8, 31), (-5, 32.5), (-2, 34), (2, 36), (8, 36)]),                 # Atlas
    (2500, 150, [(6, 23), (6, 23.5)]),                                               # Hoggar
    (3000, 150, [(17, 21), (17, 21.5)]),                                             # Tibesti
    (2500, 80, [(42, 20), (44, 16)]),                                                # Asir
    (4000, 60, [(37, -3), (37, -3.2)]),                                              # Kilimanjaro
    (3000, 100, [(27, -30), (29, -29), (30, -27)]),                                  # Drakensberg
    (3500, 200, [(-114, 49), (-110, 45), (-106, 40), (-105, 36)]),                   # Rockies
    (3000, 150, [(-120, 55), (-116, 52), (-114, 49)]),                               # Canadian Rockies
    (3500, 60, [(-121, 40), (-119, 37), (-118, 35)]),                                # Sierra Nevada
    (2500, 60, [(-122, 49), (-122, 45), (-122, 42)]),                                # Cascades
    (4000, 100, [(-152, 63), (-148, 63)]),                                           # Alaska Range
    (2000, 80, [(-160, 68), (-145, 68)]),                                            # Brooks
    (1500, 100, [(-84, 35), (-80, 38), (-76, 41), (-72, 44)]),                       # Appalachians
    (2500, 100, [(-108, 29), (-105, 24), (-103, 20)]),                               # Sierra Madre
    (2000, 60, [(-90, 15), (-85, 12), (-80, 9)]),                                    # Central America
    (5000, 150, [(-77, 8), (-78, 2), (-79, -4), (-77, -10), (-72, -16), (-68, -20),
                 (-70, -30), (-70, -40), (-72, -50)]),                               # Andes
    (1500, 100, [(147, -38), (149, -35), (150, -31), (152, -27), (148, -22), (145, -18)]),  # Great Dividing
    (3000, 50, [(168, -45), (170, -43.5), (172, -42.5)]),                            # Southern Alps
    (2500, 100, [(-45, 62), (-40, 66)]),                                             # Greenland east coast
]

# Plateaus: fill height and the polygon.
PLATEAUS = [
    (4500, [(75, 33), (80, 36), (90, 36), (100, 34), (100, 30), (92, 28), (84, 29), (78, 31)]),  # Tibet
    (1200, [(48, 30), (52, 36), (58, 37), (62, 34), (60, 28), (54, 27)]),             # Iran
    (1000, [(30, 40), (38, 40), (42, 38), (36, 36), (30, 37)]),                        # Anatolia
    (2200, [(36, 8), (37, 14), (40, 14), (42, 10), (40, 6)]),                          # Ethiopia
    (1500, [(34, 2), (38, 2), (38, -3), (34, -3)]),                                    # Kenya
    (1200, [(17, -29), (22, -33), (28, -31), (30, -25), (26, -22), (18, -26)]),        # southern Africa
    (2000, [(43, 13), (45, 16), (46, 13)]),                                            # Yemen
    (600, [(74, 22), (80, 22), (80, 14), (76, 12), (73, 18)]),                         # Deccan
    (1300, [(90, 46), (100, 50), (115, 50), (118, 44), (105, 42), (95, 44)]),          # Mongolia
    (800, [(95, 60), (110, 68), (115, 58), (100, 55)]),                                # central Siberian
    (700, [(-7, 42), (-3, 42), (-3, 38), (-7, 38)]),                                   # Meseta
    (1800, [(-114, 37), (-108, 40), (-104, 37), (-108, 34), (-112, 35)]),              # Colorado
    (1500, [(-120, 40), (-114, 42), (-112, 38), (-116, 36)]),                          # Great Basin
    (1800, [(-106, 30), (-100, 28), (-99, 21), (-104, 19), (-108, 24)]),               # Mexico
    (3800, [(-70, -14), (-66, -17), (-66, -21), (-69, -22), (-72, -17)]),              # Altiplano
    (800, [(-50, -14), (-42, -12), (-40, -20), (-45, -25), (-52, -22), (-55, -16)]),   # Brazil
    (800, [(-64, 7), (-60, 6), (-58, 3), (-63, 3)]),                                   # Guiana
    (600, [(-70, -40), (-66, -43), (-68, -50), (-70, -47)]),                           # Patagonia
    (400, [(115, -20), (125, -20), (128, -30), (120, -34), (114, -28)]),               # W Australia
]

def main():
    land = rasterise(HERE / "ne_110m_land.geojson")
    print("land fraction", land.mean())
    coast_km = distance_km_to(~land)   # distance from land to the sea
    sea_km = distance_km_to(land)      # distance from the sea to land

    elev = np.zeros((H, W), dtype=np.float64)
    # Land: a rise from the coast that settles at 400 m inland.
    elev[land] = 80.0 + np.minimum(coast_km[land], 600.0) * 0.55
    # Sea: shelf near the coast, then the deep.
    shelf = np.clip(sea_km / 250.0, 0.0, 1.0)
    elev[~land] = -150.0 - shelf[~land] ** 1.5 * 3800.0

    for height, polygon in PLATEAUS:
        m = polygon_mask(polygon) & land
        elev[m] = np.maximum(elev[m], height)
    for crest, halfwidth, line in RANGES:
        d = polyline_km(line)
        bump = crest * np.exp(-(d / halfwidth) ** 2 * 1.1)
        elev = np.where(land, np.maximum(elev, bump), elev)

    # Ice sheets: domes that rise with distance from the coast.
    greenland = polygon_mask([(-73, 78), (-20, 84), (-17, 75), (-40, 59), (-55, 60), (-62, 70)]) & land
    elev[greenland] = np.maximum(elev[greenland], 300.0 + np.minimum(coast_km[greenland], 700.0) * 3.5)
    antarctica = (LATG < -60) & land
    elev[antarctica] = np.maximum(elev[antarctica], 200.0 + np.minimum(coast_km[antarctica], 1500.0) * 2.4)

    out = np.clip(np.round(elev), -32000, 32000).astype("<i2")
    path = ROOT / "data" / "earth.bin"
    path.parent.mkdir(exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"EARTHTPL")
        f.write(struct.pack("<ii", W, H))
        f.write(out.tobytes())
    print("wrote", path, out.shape, "land max", out.max(), "sea min", out.min())

    # A quick look, for the record.
    img = np.zeros((H, W, 3), dtype=np.uint8)
    e = out.astype(np.float64)
    sea = e <= 0
    img[sea] = np.stack([np.full(sea.sum(), 20), np.full(sea.sum(), 50), np.clip(130 + e[sea] / 40, 40, 130)], 1).astype(np.uint8)
    t = np.clip(e / 4000.0, 0, 1)
    img[~sea, 0] = (60 + 180 * t[~sea]).astype(np.uint8)
    img[~sea, 1] = (120 + 100 * t[~sea]).astype(np.uint8)
    img[~sea, 2] = (50 + 200 * t[~sea]).astype(np.uint8)
    img = img[::-1]  # north up
    with open(HERE / "earth_preview.ppm", "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (W, H))
        f.write(img.tobytes())

if __name__ == "__main__":
    main()
