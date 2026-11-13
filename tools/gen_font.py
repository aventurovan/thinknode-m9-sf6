#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generuje m9_sf6/ui/FontData.h — czcionki ekranowe (Inter, licencja OFL) z polskimi znakami, 4 bity na piksel."""
import sys
from PIL import Image, ImageDraw, ImageFont

DIR = "/usr/share/fonts/opentype/inter/"
FONTS = [("F_SMALL", "Inter-Medium.otf", 11), ("F_NORMAL", "Inter-Medium.otf", 13), ("F_BOLD", "Inter-Bold.otf", 13),
         ("F_BIG", "Inter-Bold.otf", 17), ("F_HUGE", "Inter-ExtraBold.otf", 40)]
CHARS = [chr(c) for c in range(32, 127)] + list("ĄĆĘŁŃÓŚŹŻąćęłńóśźż·…„”–—°»«×◄►▲▼›")
HUGE_CHARS = list("0123456789: MeshSpd")

def gen(name, file, size, out):
    font = ImageFont.truetype(DIR + file, size)
    asc, desc = font.getmetrics()
    chars = HUGE_CHARS if name == "F_HUGE" else CHARS
    chars = sorted(chars, key=ord)
    data = bytearray()
    glyphs = []
    for ch in chars:
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
        glyphs.append((ord(ch), off, w, h, bbox[0], bbox[1], adv))
    out.append("static const uint8_t %s_bits[] PROGMEM = {" % name)
    for i in range(0, len(data), 24):
        out.append("  " + ",".join("%d" % b for b in data[i:i + 24]) + ",")
    out.append("};")
    out.append("static const GfxGlyph %s_glyphs[] PROGMEM = {" % name)
    for g in glyphs:
        out.append("  {%d,%d,%d,%d,%d,%d,%d}," % g)
    out.append("};")
    out.append("static const GfxFont %s = { %s_bits, %s_glyphs, %d, %d, %d };" % (name, name, name, len(glyphs), asc + desc, asc))
    return len(data)

out = ["// WYGENEROWANE przez tools/gen_font.py — nie edytować ręcznie.",
       "// Czcionka: Inter (SIL Open Font License 1.1), znaki ASCII + polskie, 4 bity na piksel.", "#pragma once", "#include \"Gfx.h\"", ""]
total = 0
for f in FONTS: total += gen(f[0], f[1], f[2], out); out.append("")
open(sys.argv[1] if len(sys.argv) > 1 else "m9_sf6/ui/FontData.h", "w").write("\n".join(out) + "\n")
print("bajtów:", total)
