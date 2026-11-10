#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
pakuj_mape.py — przygotowanie mapy na kartę SD dla ThinkNode M9 (firmware MC SF6).

Zamienia kafelki rastrowe 256x256 (z własnego serwera kafelków, z folderu albo z pliku .mbtiles)
na pliki .mcm: kilka dużych plików zamiast setek tysięcy małych. Opis formatu: FORMAT.md.

Przykłady:
  python3 pakuj_mape.py mapa --zrodlo "http://localhost:8080/styles/ciemna/256/{z}/{x}/{y}.png" --wyjscie karta/mapa
  python3 pakuj_mape.py mapa --zrodlo kafelki/{z}/{x}/{y}.png --obszar 18.2,49.3,20.2,50.5 --zoom 10-13 --wyjscie karta/mapa
  python3 pakuj_mape.py testowa --wyjscie karta/mapa          (mapa testowa: siatka i miasta, bez danych OSM)
  python3 pakuj_mape.py info karta/mapa/z13_x4416.mcm
  python3 pakuj_mape.py podglad karta/mapa/z13_x4416.mcm 4529 2788 kafelek.png

Wymaga: pip install pillow numpy
"""
import argparse, concurrent.futures, glob, io, math, os, sqlite3, struct, sys, time, urllib.request, urllib.error
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HEADER = 32
POLSKA = (14.07, 48.95, 24.20, 54.90)      # zachód, południe, wschód, północ
POLUDNIE = (14.80, 48.95, 24.20, 51.20)
LIMIT_PLIKU = 3_000_000_000                # FAT32 nie przyjmie pliku powyżej 4 GB — dzielimy wcześniej


# ---------- współrzędne ----------
def lonlat_to_tile(lon, lat, z):
    n = 2 ** z
    x = (lon + 180.0) / 360.0 * n
    r = math.radians(lat)
    y = (1.0 - math.log(math.tan(r) + 1.0 / math.cos(r)) / math.pi) / 2.0 * n
    return x, y

def tile_range(bbox, z):
    w, s, e, n = bbox
    x0, y1 = lonlat_to_tile(w, s, z)
    x1, y0 = lonlat_to_tile(e, n, z)
    m = 2 ** z - 1
    return max(0, int(x0)), max(0, int(y0)), min(m, int(x1)), min(m, int(y1))


# ---------- kodowanie kafelka ----------
def rle(idx):
    """idx: płaska tablica uint8 (65536 indeksów palety 0..15) -> bajty serii."""
    n = idx.size
    ch = np.flatnonzero(idx[1:] != idx[:-1]) + 1
    starts = np.concatenate(([0], ch))
    lens = np.diff(np.concatenate((starts, [n])))
    cols = idx[starts].astype(np.uint32)
    # bardzo długie serie (powyżej 65806 pikseli nie ma: kafelek ma 65536)
    size = np.where(lens <= 14, 1, np.where(lens <= 270, 2, 3))
    off = np.concatenate(([0], np.cumsum(size)))
    out = np.zeros(int(off[-1]), dtype=np.uint8)
    o = off[:-1]
    a = lens <= 14
    out[o[a]] = (cols[a] << 4) | (lens[a] - 1)
    b = (lens > 14) & (lens <= 270)
    out[o[b]] = (cols[b] << 4) | 14
    out[o[b] + 1] = lens[b] - 15
    c = lens > 270
    out[o[c]] = (cols[c] << 4) | 15
    v = lens[c] - 271
    out[o[c] + 1] = v & 255
    out[o[c] + 2] = v >> 8
    return out.tobytes()

def encode_tile(img):
    """Obraz PIL -> bajty kafelka MCM (paleta do 16 kolorów RGB565 + serie)."""
    if img.size != (256, 256):
        img = img.resize((256, 256), Image.LANCZOS)
    q = img.convert("RGB").quantize(16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    idx = np.asarray(q, dtype=np.uint8).reshape(-1)
    nc = int(idx.max()) + 1
    pal = q.getpalette()[:nc * 3]
    head = bytearray([nc])
    for i in range(nc):
        r, g, b = pal[i * 3:i * 3 + 3]
        head += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return bytes(head) + rle(idx)

def decode_tile(data):
    """Bajty kafelka MCM -> obraz PIL (ten sam algorytm co w firmware, do podglądu i testów)."""
    nc = data[0]
    pal = []
    for i in range(nc):
        v = struct.unpack_from("<H", data, 1 + i * 2)[0]
        pal.append((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
    px = np.zeros(65536, dtype=np.uint8)
    p, pos = 1 + nc * 2, 0
    while pos < 65536:
        b = data[p]; p += 1
        c, n = b >> 4, b & 15
        if n < 14: run = n + 1
        elif n == 14: run = 15 + data[p]; p += 1
        else: run = 271 + data[p] + (data[p + 1] << 8); p += 2
        px[pos:pos + run] = c
        pos += run
    img = Image.fromarray(px.reshape(256, 256), "P")
    flat = []
    for c in pal: flat += list(c)
    img.putpalette(flat + [0] * (768 - len(flat)))
    return img


# ---------- plik .mcm ----------
class Pack:
    """Zapis jednego pliku .mcm. Indeks jest kolumnami, więc plik można zamknąć po dowolnej liczbie kolumn."""
    def __init__(self, folder, zoom, x0, y0, nx_max, ny):
        self.zoom, self.x0, self.y0, self.ny = zoom, x0, y0, ny
        self.path = os.path.join(folder, "z%d_x%d.mcm" % (zoom, x0))
        self.tmp = self.path + ".tmp"
        self.f = open(self.tmp, "wb")
        self.index = np.zeros(nx_max * ny, dtype=np.uint64)
        self.f.write(b"\0" * (HEADER + nx_max * ny * 8))
        self.pos = self.f.tell()
        self.cols = 0
        self.tiles = 0

    def add_column(self, tiles):
        for j, t in enumerate(tiles):
            if not t: continue
            self.index[self.cols * self.ny + j] = self.pos | (len(t) << 40)
            self.f.write(t)
            self.pos += len(t)
            self.tiles += 1
        self.cols += 1

    def close(self):
        self.f.seek(0)
        self.f.write(struct.pack("<4sBBHIIIIII", b"MCM1", self.zoom, 8, HEADER, self.x0, self.y0, self.cols, self.ny, self.tiles, 0))
        self.f.write(self.index[:self.cols * self.ny].astype("<u8").tobytes())
        self.f.close()
        os.replace(self.tmp, self.path)
        return self.path

def read_header(f):
    h = f.read(HEADER)
    magic, zoom, tl, hs, x0, y0, nx, ny, tiles, _ = struct.unpack("<4sBBHIIIIII", h)
    if magic != b"MCM1": raise SystemExit("To nie jest plik MCM1")
    return dict(zoom=zoom, x0=x0, y0=y0, nx=nx, ny=ny, tiles=tiles)

def read_tile(path, x, y):
    with open(path, "rb") as f:
        h = read_header(f)
        if not (h["x0"] <= x < h["x0"] + h["nx"] and h["y0"] <= y < h["y0"] + h["ny"]): return None
        f.seek(HEADER + ((x - h["x0"]) * h["ny"] + (y - h["y0"])) * 8)
        e = struct.unpack("<Q", f.read(8))[0]
        off, ln = e & ((1 << 40) - 1), e >> 40
        if ln == 0: return None
        f.seek(off)
        return f.read(ln)


# ---------- źródła kafelków ----------
class Zrodlo:
    def __init__(self, spec):
        self.spec, self.db = spec, None
        if spec.lower().endswith(".mbtiles"):
            self.kind = "mbtiles"
        elif spec.startswith("http://") or spec.startswith("https://"):
            self.kind = "url"
            if "openstreetmap.org" in spec:
                raise SystemExit("Serwery OpenStreetMap nie pozwalają na masowe pobieranie kafelków. Użyj własnego serwera (README.md).")
        else:
            self.kind = "dir"

    def get(self, z, x, y):
        if self.kind == "url":
            url = self.spec.format(z=z, x=x, y=y)
            for proba in range(4):
                try:
                    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "pakuj_mape/1.0"}), timeout=60) as r:
                        return r.read()
                except urllib.error.HTTPError as e:
                    if e.code in (204, 404): return None
                    time.sleep(1 + proba)
                except Exception:
                    time.sleep(1 + proba)
            raise SystemExit("Nie udało się pobrać kafelka: " + url)
        if self.kind == "dir":
            p = self.spec.format(z=z, x=x, y=y)
            return open(p, "rb").read() if os.path.exists(p) else None
        db = sqlite3.connect(self.spec)   # osobne połączenie na wywołanie: bezpieczne w wątkach
        try:
            row = db.execute("SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?", (z, x, (2 ** z - 1) - y)).fetchone()
        finally:
            db.close()
        return row[0] if row else None

def fetch_encode(src, z, x, y):
    raw = src.get(z, x, y)
    if not raw: return None
    return encode_tile(Image.open(io.BytesIO(raw)))


# ---------- pakowanie ----------
def gotowe_kolumny(folder, zoom):
    """Wznawianie: kolumny, które są już w zamkniętych plikach tego zoomu."""
    done = 0
    for p in glob.glob(os.path.join(folder, "z%d_x*.mcm" % zoom)):
        with open(p, "rb") as f:
            h = read_header(f)
        done = max(done, h["x0"] + h["nx"])
    return done

def pakuj(src, folder, bbox, zoom, watki, getter=None):
    os.makedirs(folder, exist_ok=True)
    x0, y0, x1, y1 = tile_range(bbox, zoom)
    ny = y1 - y0 + 1
    start = max(x0, gotowe_kolumny(folder, zoom))
    if start > x1:
        print("zoom %d: gotowe, pomijam" % zoom); return
    total = (x1 - start + 1) * ny
    print("zoom %d: kolumny %d–%d, wiersze %d–%d, kafelków %d" % (zoom, start, x1, y0, y1, total))
    t0, done = time.time(), 0
    pack = Pack(folder, zoom, start, y0, x1 - start + 1, ny)
    with concurrent.futures.ThreadPoolExecutor(watki) as ex:
        for x in range(start, x1 + 1):
            if getter: tiles = [getter(zoom, x, y) for y in range(y0, y1 + 1)]
            else: tiles = list(ex.map(lambda y: fetch_encode(src, zoom, x, y), range(y0, y1 + 1)))
            pack.add_column(tiles)
            done += ny
            if x == x1 or (x - start) % max(1, (x1 - start) // 50) == 0:
                el = time.time() - t0
                eta = el / done * (total - done)
                print("  %5.1f%%  %d kafelków  %.0f MB  zostało ok. %d min" % (100.0 * done / total, done, pack.pos / 1e6, eta / 60), flush=True)
            if pack.pos > LIMIT_PLIKU and x < x1:
                print("  zamykam", pack.close())
                pack = Pack(folder, zoom, x + 1, y0, x1 - x, ny)
    print("  zapisano", pack.close())


# ---------- mapa testowa (bez danych OSM) ----------
MIASTA = [("Bielsko-Biała", 49.8225, 19.0469, 8), ("Katowice", 50.2649, 19.0238, 6), ("Kraków", 50.0647, 19.9450, 6), ("Warszawa", 52.2297, 21.0122, 6),
          ("Wrocław", 51.1079, 17.0385, 6), ("Gdańsk", 54.3520, 18.6466, 6), ("Poznań", 52.4064, 16.9252, 6), ("Szczyrk", 49.7150, 19.0330, 10),
          ("Skrzyczne", 49.6845, 19.0303, 11), ("Żywiec", 49.6853, 19.1924, 10), ("Cieszyn", 49.7500, 18.6333, 9), ("Ustroń", 49.7215, 18.8020, 10),
          ("Pszczyna", 49.9781, 18.9540, 10), ("Tychy", 50.1372, 18.9664, 9), ("Rzeszów", 50.0413, 21.9990, 7), ("Opole", 50.6751, 17.9213, 7)]

def czcionka(size):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf", "C:/Windows/Fonts/arial.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf"):
        if os.path.exists(p): return ImageFont.truetype(p, size)
    return ImageFont.load_default()

def kafelek_testowy(z, x, y, fonts):
    img = Image.new("RGB", (256, 256), (16, 23, 31))
    d = ImageDraw.Draw(img)
    for k in range(0, 256, 64):
        d.line([(k, 0), (k, 255)], fill=(24, 34, 46)); d.line([(0, k), (255, k)], fill=(24, 34, 46))
    d.rectangle([0, 0, 255, 255], outline=(58, 70, 86))
    d.text((6, 4), "%d/%d/%d" % (z, x, y), fill=(154, 167, 180), font=fonts[0])
    for name, lat, lon, minz in MIASTA:
        if z < minz: continue
        tx, ty = lonlat_to_tile(lon, lat, z)
        px, py = (tx - x) * 256, (ty - y) * 256
        if -80 < px < 336 and -20 < py < 276:
            d.ellipse([px - 4, py - 4, px + 4, py + 4], fill=(255, 176, 32))
            d.text((px + 7, py - 8), name, fill=(242, 245, 247), font=fonts[1])
    return encode_tile(img)

def testowa(folder):
    fonts = (czcionka(11), czcionka(13))
    for z in range(6, 10):
        pakuj(None, folder, POLSKA, z, 1, getter=lambda zz, x, y: kafelek_testowy(zz, x, y, fonts))
    for z in range(10, 14):
        pakuj(None, folder, (18.2, 49.3, 20.2, 50.5), z, 1, getter=lambda zz, x, y: kafelek_testowy(zz, x, y, fonts))


# ---------- polecenia ----------
def main():
    ap = argparse.ArgumentParser(description="Mapa na kartę SD dla ThinkNode M9 (MC SF6)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("mapa", help="spakuj kafelki do plików .mcm")
    m.add_argument("--zrodlo", required=True, help="adres z {z} {x} {y}, ścieżka z {z} {x} {y} albo plik .mbtiles")
    m.add_argument("--wyjscie", required=True, help="folder wynikowy (na karcie: /mapa)")
    m.add_argument("--obszar", help="polska | poludnie | zachód,południe,wschód,północ (domyślnie: plan Polska 6–13 + południe 14–15)")
    m.add_argument("--zoom", help="np. 6-13 albo 12")
    m.add_argument("--watki", type=int, default=8)
    t = sub.add_parser("testowa", help="mapa testowa: siatka, numery kafelków i miasta")
    t.add_argument("--wyjscie", required=True)
    i = sub.add_parser("info", help="pokaż nagłówek pliku .mcm")
    i.add_argument("plik")
    p = sub.add_parser("podglad", help="zapisz jeden kafelek jako PNG")
    p.add_argument("plik"); p.add_argument("x", type=int); p.add_argument("y", type=int); p.add_argument("png")
    a = ap.parse_args()

    if a.cmd == "testowa":
        testowa(a.wyjscie)
    elif a.cmd == "info":
        with open(a.plik, "rb") as f: h = read_header(f)
        print("zoom %(zoom)d, kolumny od %(x0)d (%(nx)d), wiersze od %(y0)d (%(ny)d), kafelków %(tiles)d" % h)
    elif a.cmd == "podglad":
        t = read_tile(a.plik, a.x, a.y)
        if not t: raise SystemExit("Brak takiego kafelka w pliku")
        decode_tile(t).convert("RGB").save(a.png); print("zapisano", a.png, "(%d bajtów w pliku mapy)" % len(t))
    else:
        src = Zrodlo(a.zrodlo)
        if a.obszar or a.zoom:
            ob = {"polska": POLSKA, "poludnie": POLUDNIE}.get(a.obszar or "polska") or tuple(float(v) for v in a.obszar.split(","))
            zs = (a.zoom or "6-13").split("-")
            plan = [(ob, z) for z in range(int(zs[0]), int(zs[-1]) + 1)]
        else:
            plan = [(POLSKA, z) for z in range(6, 14)] + [(POLUDNIE, z) for z in (14, 15)]
        for ob, z in plan:
            pakuj(src, a.wyjscie, ob, z, a.watki)

if __name__ == "__main__":
    main()
