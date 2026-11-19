#!/usr/bin/env python3
"""Download offline map tiles for EasyDeck on a computer - much faster than over the T-Deck's Wi-Fi.

Writes standard 256 px PNG tiles to <out>/<z>/<x>/<y>.png (the layout EasyDeck and Ripple read
from /tiles on the SD card). Tiles already there are skipped, so you can stop and resume.

Example (MidCoast NSW, 40 km around Taree, street detail, SD card mounted on Linux):
  python3 tools/get_tiles.py --url "https://tile.thunderforest.com/outdoors/{z}/{x}/{y}.png?apikey=YOURKEY" \\
      --lat -31.911 --lon 152.463 --radius-km 40 --zmax 15 --out /run/media/$USER/SDCARD/tiles

Use a tile source whose terms allow downloading areas for offline use. OpenStreetMap's own tile
servers do NOT allow this. Thunderforest needs a (free Hobby) API key. Standard library only.
"""
import argparse, math, os, sys, time, urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed

TILE = 256

def lon2x(lon, z): return (lon + 180.0) / 360.0 * (1 << z) * TILE

def lat2y(lat, z):
    r = math.radians(lat)
    return (1.0 - math.log(math.tan(r) + 1.0 / math.cos(r)) / math.pi) / 2.0 * (1 << z) * TILE

def tile_range(lat, lon, radius_km, z):   # same maths as firmware/src/ui/MapView.cpp
    dlat = radius_km / 111.32
    c = math.cos(math.radians(lat))
    dlon = radius_km / (111.32 * max(c, 0.05))
    la0, la1 = max(-85.0, lat - dlat), min(85.0, lat + dlat)
    lo0, lo1 = max(-180.0, lon - dlon), min(179.9999, lon + dlon)
    n = 1 << z
    x0, x1 = int(lon2x(lo0, z) // TILE), int(lon2x(lo1, z) // TILE)
    y0, y1 = int(lat2y(la1, z) // TILE), int(lat2y(la0, z) // TILE)
    return max(0, x0), min(n - 1, x1), max(0, y0), min(n - 1, y1)

def fetch(url, path, retries=3):
    for attempt in range(retries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "EasyDeck tile tool (offline maps for LilyGO T-Deck)"})
            with urllib.request.urlopen(req, timeout=30) as r:
                data = r.read()
            if not data.startswith(b"\x89PNG"):
                return "not a PNG"
            if data[24] > 8:
                return "16-bit PNG (EasyDeck needs 8-bit): choose another source"
            os.makedirs(os.path.dirname(path), exist_ok=True)
            tmp = path + ".part"
            with open(tmp, "wb") as f:
                f.write(data)
            os.replace(tmp, path)
            return len(data)
        except urllib.error.HTTPError as e:
            if e.code in (401, 403, 404):
                return f"HTTP {e.code}"
            time.sleep(1 + attempt)
        except Exception as e:   # network hiccup: retry
            time.sleep(1 + attempt)
            err = str(e)
    return "failed"

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", required=True, help="tile URL template with {z} {x} {y}")
    ap.add_argument("--lat", type=float, required=True)
    ap.add_argument("--lon", type=float, required=True)
    ap.add_argument("--radius-km", type=float, default=25)
    ap.add_argument("--zmin", type=int, default=6)
    ap.add_argument("--zmax", type=int, default=15)
    ap.add_argument("--out", required=True, help="folder that will hold <z>/<x>/<y>.png (e.g. SDCARD/tiles)")
    ap.add_argument("--workers", type=int, default=8, help="parallel downloads (be kind to the server)")
    a = ap.parse_args()
    if not all(k in a.url for k in ("{z}", "{x}", "{y}")):
        sys.exit("--url needs {z}, {x} and {y}")

    jobs = []
    for z in range(a.zmin, a.zmax + 1):
        x0, x1, y0, y1 = tile_range(a.lat, a.lon, a.radius_km, z)
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                p = os.path.join(a.out, str(z), str(x), f"{y}.png")
                if not os.path.exists(p):
                    jobs.append((a.url.replace("{z}", str(z)).replace("{x}", str(x)).replace("{y}", str(y)), p))
    total = len(jobs)
    print(f"{total} tiles to download (others already there)")
    done = fails = 0
    size = 0
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=max(1, min(a.workers, 16))) as ex:
        futs = [ex.submit(fetch, u, p) for u, p in jobs]
        for f in as_completed(futs):
            r = f.result()
            done += 1
            if isinstance(r, int): size += r
            else:
                fails += 1
                if fails <= 5: print(f"  problem: {r}")
                if fails == 20 and done == fails: sys.exit("Every download fails - check the URL / API key.")
            if done % 200 == 0 or done == total:
                rate = done / max(0.1, time.time() - t0)
                print(f"  {done}/{total}  {size / 1e6:.1f} MB  {rate:.0f} tiles/s  {fails} failed", flush=True)
    print("Done. Put the tiles folder on the SD card root as /tiles (if --out wasn't already there).")

if __name__ == "__main__":
    main()
