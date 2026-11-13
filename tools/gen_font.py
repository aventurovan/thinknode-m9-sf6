#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generuje m9_sf6/ui/FontData.h — czcionki ekranowe z polskimi znakami, 4 bity na piksel.
Czcionka: IBM Plex Sans (jak w aplikacji MeshSpeed, licencja OFL, pliki w tools/fonts); znaki, których w niej nie ma
(strzałki), biorą się z Inter (OFL, z systemu)."""
import sys, os
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
DIR = os.path.join(HERE, "fonts") + "/"
FALLBACK = "/usr/share/fonts/opentype/inter/"
FONTS = [("F_SMALL", "IBMPlexSans-Medium.ttf", 11, "Inter-Medium.otf"), ("F_NORMAL", "IBMPlexSans-Medium.ttf", 13, "Inter-Medium.otf"), ("F_BOLD", "IBMPlexSans-Bold.ttf", 13, "Inter-Bold.otf"),
         ("F_BIG", "IBMPlexSans-Bold.ttf", 17, "Inter-Bold.otf"), ("F_HUGE", "IBMPlexSans-Bold.ttf", 40, "Inter-ExtraBold.otf")]
CHARS = [chr(c) for c in range(32, 127)] + list("ĄĆĘŁŃÓŚŹŻąćęłńóśźż·…„”–—°»«×◄►▲▼›→")
HUGE_CHARS = list("0123456789: MeshSpd")

def gen(name, file, size, fb_file, out):
    font = ImageFont.truetype(DIR + file, size)
    have = set(TTFont(DIR + file).getBestCmap().keys())
    fb = ImageFont.truetype(FALLBACK + fb_file, size) if os.path.exists(FALLBACK + fb_file) else font
    main_font = font
    shift = max(1, round(size * 0.13))            # Plex ma wyższą linię górną niż Inter: podnosimy znaki, żeby tekst siedział w wierszach jak dotąd
    asc, desc = font.getmetrics()
    chars = HUGE_CHARS if name == "F_HUGE" else CHARS
    chars = sorted(chars, key=ord)
    data = bytearray()
    glyphs = []
    for ch in chars:
        font = main_font if ord(ch) in have else fb
        adv = int(round(font.getlength(ch)))
        bbox = font.getbbox(ch)                      # względem linii górnej (ascender = 0)
        w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
        if w <= 0 or h <= 0:
            glyphs.append((ord(ch), len(data), 0, 0, 0, 0, adv)); continue
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-bbox[0], -bbox[1]), ch, font=font, fill=255)
        px = img.tobytes()
        off = len(data)
        nib = [min(15, (p + 8) // 17) for p in px]
        if len(nib) % 2: nib.append(0)
        for i in range(0, len(nib), 2): data.append((nib[i] << 4) | nib[i + 1])
        glyphs.append((ord(ch), off, w, h, bbox[0], bbox[1] - shift, adv))
    out.append("static const uint8_t %s_bits[] PROGMEM = {" % name)
    for i in range(0, len(data), 24):
        out.append("  " + ",".join("%d" % b for b in data[i:i + 24]) + ",")
    out.append("};")
    out.append("static const GfxGlyph %s_glyphs[] PROGMEM = {" % name)
    for g in glyphs:
        out.append("  {%d,%d,%d,%d,%d,%d,%d}," % g)
    out.append("};")
    asc, desc = main_font.getmetrics()
    out.append("static const GfxFont %s = { %s_bits, %s_glyphs, %d, %d, %d };" % (name, name, name, len(glyphs), asc + desc, asc))
    return len(data)

out = ["// WYGENEROWANE przez tools/gen_font.py — nie edytować ręcznie.",
       "// Czcionka: IBM Plex Sans (SIL Open Font License 1.1), strzałki z Inter (OFL); znaki ASCII + polskie, 4 bity na piksel.", "#pragma once", "#include \"Gfx.h\"", ""]
total = 0
for f in FONTS: total += gen(f[0], f[1], f[2], f[3], out); out.append("")
open(sys.argv[1] if len(sys.argv) > 1 else "m9_sf6/ui/FontData.h", "w").write("\n".join(out) + "\n")
print("bajtów:", total)
