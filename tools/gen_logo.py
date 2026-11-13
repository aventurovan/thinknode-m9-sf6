#!/usr/bin/env python3
"""Generuje m9_sf6/ui/LogoData.h — logo MeshSpeed (z ikony aplikacji) jako obrazek RGB565 na tle ekranu startowego."""
import os
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
BG = (0x0B, 0x0F, 0x14)
SIZE = 84
im = Image.open(os.path.join(HERE, "logo", "meshspeed-foreground.png")).convert("RGBA")
im = im.crop(im.getbbox())
w, h = im.size
s = SIZE / max(w, h)
im = im.resize((max(1, round(w * s)), max(1, round(h * s))), Image.LANCZOS)
bg = Image.new("RGBA", im.size, BG + (255,))
bg.alpha_composite(im)
px = list(bg.convert("RGB").getdata())
w, h = bg.size
out = ["// WYGENEROWANE przez tools/gen_logo.py — nie edytować ręcznie.", "#pragma once", "#include <stdint.h>", "#ifndef PROGMEM", "  #define PROGMEM", "#endif",
       "#define LOGO_W %d" % w, "#define LOGO_H %d" % h, "static const uint16_t LOGO_PX[] PROGMEM = {"]
vals = ["%d" % (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)) for r, g, b in px]
for i in range(0, len(vals), 20): out.append("  " + ",".join(vals[i:i + 20]) + ",")
out.append("};")
open(os.path.join(HERE, "..", "m9_sf6", "ui", "LogoData.h"), "w").write("\n".join(out) + "\n")
print("logo", w, h)
