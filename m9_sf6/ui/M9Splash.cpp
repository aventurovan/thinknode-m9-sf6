// Ekran startowy MeshSpeed rysowany od razu po uruchomieniu ekranu (zamiast napisu „Loading...” z MeshCore).
#include <Arduino.h>
#include "UiView.h"
#include "M9Lcd.h"
#include "../Version.h"

void m9BootSplash() {
  uint16_t* fb = (uint16_t*)ps_malloc(GFX_W * GFX_H * 2);
  UiView* v = (UiView*)ps_calloc(1, sizeof(UiView));
  if (fb && v) {
    v->screen = UIS_BOOT;
    snprintf(v->line1, sizeof(v->line1), "ThinkNode M9 · wersja %s", M9_FW_VERSION);
    strcpy(v->line2, "uruchamianie…");
    Gfx g(fb);
    uiRender(g, *v);
    g.swapBytes();
    m9lcdBlit(0, 0, GFX_W, GFX_H, fb);
  }
  if (fb) free(fb);
  if (v) free(v);
}
