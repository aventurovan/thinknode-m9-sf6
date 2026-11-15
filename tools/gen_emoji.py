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

# podstawowy zestaw: buźki, gesty, serca, pogoda, przyroda, jedzenie, pojazdy, przedmioty, symbole
EMOJI = (
    "😀😃😄😁😆😅😂🤣😊🙂🙃😉😍🥰😘😋😜🤪😎🤓🤔🤨😐😑😶🙄😏😬😴😷🤒🤢🥵🥶😵🤯🥳😕😟🙁😮😲😳🥺😢😭😱😤😡🤬😈💀💩🤡👻🤖"
    "👍👎👌✌🤞🤟🤙👋🖐✋👊✊🙌👏🙏🤝💪👀👉👈👆👇🤷🤦"
    "❤🧡💛💚💙💜🖤💔💕💯🔥✨⭐🌟🎉🎁🏆"
    "☀🌤⛅☁🌧⛈🌩❄☔🌈🌙⚡💧🌊🌡💨"
    "🐶🐱🐟🎣🐻🦊🐝🌲🌳🌻🍀🏔⛰🏕⛺"
    "☕🍺🍻🍷🍕🍔🍰🍎"
    "🚗🚐🚲✈🚀⛽🏠📍🗺🧭"
    "📡📻📱💻🔋🔌💡🔧🔨⚙🔑🔒🔔🔕📷🎵📢⏰⌛📅📌✉💬"
    "✅❌⚠❓❗⛔🚫🆗🆘➡⬅⬆⬇♻🔴🟢🟡🔵"
)
FLAGS = ["PL", "CZ", "SK", "DE", "UA", "AT", "HU", "IT", "GB", "US", "NO", "HR"]

SIZES = (16, 12)


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
for ch in EMOJI:
    cp = ord(ch)
    if cp in (0xFE0F, 0x200D):
        continue
    im = render(ch + '️') or render(ch)
    if im is None:
        print('brak w czcionce: U+%X' % cp)
        continue
    items[cp] = im
for fl in FLAGS:
    txt = ''.join(chr(0x1F1E6 + ord(c) - 65) for c in fl)
    im = render(txt)
    if im is None or im.width > 150:
        print('brak flagi', fl)
        continue
    items[0x80000000 | (ord(fl[0]) << 8) | ord(fl[1])] = im

keys = sorted(items)
with open(OUT, 'w', encoding='utf-8') as o:
    o.write('// WYGENEROWANE przez tools/gen_emoji.py — nie edytować ręcznie.\n')
    o.write('// Emotki: Noto Color Emoji (SIL Open Font License 1.1). Klucz: kod Unicode; flagi: 0x80000000 | litera1 << 8 | litera2.\n')
    o.write('#pragma once\n#include <stdint.h>\n#ifndef PROGMEM\n  #define PROGMEM\n#endif\n\n')
    o.write('#define EMOJI_N %d\n\n' % len(keys))
    o.write('static const uint32_t EMOJI_CP[EMOJI_N] PROGMEM = {\n')
    for i in range(0, len(keys), 8):
        o.write('  ' + ','.join('0x%X' % k for k in keys[i:i + 8]) + ',\n')
    o.write('};\n\n')
    for size in SIZES:
        rgbs, als = [], []
        for k in keys:
            r, a = pack(items[k], size)
            rgbs += r
            als += a
        o.write('static const uint16_t EMOJI%d_RGB[] PROGMEM = {\n' % size)
        for i in range(0, len(rgbs), 16):
            o.write('  ' + ','.join(str(v) for v in rgbs[i:i + 16]) + ',\n')
        o.write('};\nstatic const uint8_t EMOJI%d_A[] PROGMEM = {\n' % size)
        for i in range(0, len(als), 24):
            o.write('  ' + ','.join(str(v) for v in als[i:i + 24]) + ',\n')
        o.write('};\n\n')
print('emotek:', len(keys), ' dane: %d kB' % (len(keys) * (256 * 2 + 128 + 144 * 2 + 72) // 1024))
