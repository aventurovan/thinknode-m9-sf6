#!/usr/bin/env python3
"""Generuje m9_sf6/ui/EmojiData.h — obrazki emotek do tekstu wiadomości.

Źródło: czcionka Noto Color Emoji (SIL Open Font License 1.1). Każda emotka w dwóch rozmiarach
(16 px do zwykłego tekstu, 12 px do małego), jako RGB565 + 4 bity przezroczystości na piksel.
Użycie: python3 tools/gen_emoji.py [ścieżka do NotoColorEmoji.ttf]
"""
import sys
from PIL import Image, ImageFont, ImageDraw

FONT = sys.argv[1] if len(sys.argv) > 1 else '/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf'
OUT = 'm9_sf6/ui/EmojiData.h'

# Zestaw: wszystkie pojedyncze emotki z czcionki (bez odcieni skóry — urządzenie pokazuje wersję podstawową)
# i flagi krajów. Emotki złożone z kilku znaków (rodziny, zawody) rysują się jako swoje części.
from fontTools.ttLib import TTFont
CMAP = TTFont(FONT).getBestCmap()
SINGLE = [k for k in sorted(CMAP) if 0x203C <= k < 0xF0000 and not (0x1F3FB <= k <= 0x1F3FF) and not (0x1F1E6 <= k <= 0x1F1FF)
          and k not in (0xFE0F, 0xFE0E, 0x200D, 0x20E3)]
# flagi: pary liter regionu, dla których czcionka ma wspólny obrazek (ligaturę)
_F = TTFont(FONT)
_RI = {CMAP[0x1F1E6 + i]: chr(65 + i) for i in range(26) if 0x1F1E6 + i in CMAP}
FLAGS = []
for lk in _F['GSUB'].table.LookupList.Lookup:
    for st in lk.SubTable:
        st = getattr(st, 'ExtSubTable', st)
        for first, ligs in getattr(st, 'ligatures', {}).items():
            for lg in ligs:
                if first in _RI and len(lg.Component) == 1 and lg.Component[0] in _RI:
                    FLAGS.append(_RI[first] + _RI[lg.Component[0]])
FLAGS = sorted(set(FLAGS))

SIZE = 16


def render(text):
    f = ImageFont.truetype(FONT, 109)
    im = Image.new('RGBA', (320, 200), (0, 0, 0, 0))
    ImageDraw.Draw(im).text((20, 20), text, font=f, embedded_color=True)
    bb = im.getbbox()
    if not bb:
        return None
    im = im.crop(bb)
    side = max(im.size)
    sq = Image.new('RGBA', (side, side), (0, 0, 0, 0))
    sq.paste(im, ((side - im.width) // 2, (side - im.height) // 2))
    return sq


def pack(im, size):
    im = im.resize((size, size), Image.LANCZOS)
    rgb, al = [], []
    for y in range(size):
        for x in range(size):
            r, g, b, a = im.getpixel((x, y))
            a4 = (a * 15 + 127) // 255
            rgb.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3) if a4 else 0)
            al.append(a4)
    if len(al) % 2:
        al.append(0)
    return rgb, [(al[i] << 4) | al[i + 1] for i in range(0, len(al), 2)]


items = {}
for cp in SINGLE:
    im = render(chr(cp) + '\ufe0f') or render(chr(cp))
    if im is None:
        continue
    items[cp] = im
nflags = 0
for fl in FLAGS:
    txt = ''.join(chr(0x1F1E6 + ord(c) - 65) for c in fl)
    im = render(txt)
    if im is None:
        continue
    items[0x80000000 | (ord(fl[0]) << 8) | ord(fl[1])] = im
    nflags += 1

keys = sorted(items)
with open(OUT, 'w', encoding='utf-8') as o:
    o.write('// WYGENEROWANE przez tools/gen_emoji.py — nie edytować ręcznie.\n')
    o.write('// Emotki: Noto Color Emoji (SIL Open Font License 1.1). Klucz: kod Unicode; flagi: 0x80000000 | litera1 << 8 | litera2.\n')
    o.write('// Obrazki 16x16: RGB565 i osobno 4 bity przezroczystości na piksel.\n')
    o.write('#pragma once\n#include <stdint.h>\n#ifndef PROGMEM\n  #define PROGMEM\n#endif\n\n')
    o.write('#define EMOJI_N %d\n\n' % len(keys))
    o.write('static const uint32_t EMOJI_CP[EMOJI_N] PROGMEM = {\n')
    for i in range(0, len(keys), 8):
        o.write('  ' + ','.join('0x%X' % k for k in keys[i:i + 8]) + ',\n')
    o.write('};\n\n')
    rgbs, als = [], []
    for k in keys:
        r, a = pack(items[k], SIZE)
        rgbs += r
        als += a
    o.write('static const uint16_t EMOJI16_RGB[] PROGMEM = {\n')
    for i in range(0, len(rgbs), 32):
        o.write(','.join(str(v) for v in rgbs[i:i + 32]) + ',\n')
    o.write('};\nstatic const uint8_t EMOJI16_A[] PROGMEM = {\n')
    for i in range(0, len(als), 64):
        o.write(','.join(str(v) for v in als[i:i + 64]) + ',\n')
    o.write('};\n')
print('emotek:', len(keys), '(w tym flag: %d)' % nflags, ' dane: %d kB' % (len(keys) * (256 * 2 + 128 + 4) // 1024))
