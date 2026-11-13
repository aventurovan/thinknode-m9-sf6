#!/usr/bin/env python3
"""Zamienia napis „Loading...” z main.cpp MeshCore na ekran startowy MeshSpeed (logo), rysowany od razu po włączeniu ekranu.
Uruchamiane przez workflow na skopiowanym main.cpp."""
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
a = '''    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
'''
assert a in s, "nie znaleziono napisu Loading"
s = s.replace(a, "    m9BootSplash();\n", 1)
b = "/* GLOBAL OBJECTS */"
assert b in s
s = s.replace(b, "extern void m9BootSplash();   // m9_sf6/ui/M9Splash.cpp\n\n" + b, 1)
open(p, "w", encoding="utf-8").write(s)
print("patch main: ok")
