/* See srw1_interp.h. */
#include "srw1_interp.h"
#include "snes/ppu.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define kRegBytes offsetof(Ppu, vramPointer) /* register snapshot prefix */
#define kLines 225
#define kBgMaxDelta 24  /* px per tick; larger = teleport/scene cut */
#define kObjMaxDelta 24
#define kObjSearchRadius 16

typedef struct Capture {
  Ppu *start;                      /* full PPU at the frame's first line */
  uint8_t regs[kLines][kRegBytes]; /* per-line registers (HDMA/IRQ aware) */
  uint16_t cgram[kLines][0x100];   /* per-line CGRAM (HDMA gradients) */
  uint32_t serial;
  bool complete;
} Capture;

static Srw1MotionMode g_mode = kSrw1Motion_Interpolate;
static Capture g_cap[2];
static int g_cur = -1;      /* index of the newest complete capture */
static int g_filling = 0;   /* capture being written by the live raster */
static uint32_t g_serial;
static Ppu *g_scratch;

Srw1MotionMode Srw1Interp_GetMode(void) { return g_mode; }
void Srw1Interp_SetMode(Srw1MotionMode m) {
  g_mode = m;
  if (m == kSrw1Motion_Off) g_cur = -1; /* drop history; restart cleanly */
}

void Srw1Interp_CaptureLine(Ppu *ppu, int line) {
  if (g_mode == kSrw1Motion_Off || line < 0 || line >= kLines) return;
  Capture *c = &g_cap[g_filling];
  if (line == 0) {
    if (!c->start) c->start = (Ppu *)malloc(sizeof(Ppu));
    if (!c->start) return;
    memcpy(c->start, ppu, sizeof(Ppu));
    c->complete = false;
  } else if (!c->start) {
    return;
  }
  memcpy(c->regs[line], ppu, kRegBytes);
  memcpy(c->cgram[line], ppu->cgram, sizeof(ppu->cgram));
}

void Srw1Interp_EndFrame(void) {
  if (g_mode == kSrw1Motion_Off) return;
  Capture *c = &g_cap[g_filling];
  if (!c->start) return;
  c->complete = true;
  c->serial = ++g_serial;
  g_cur = g_filling;
  g_filling ^= 1;
}

static inline int wrap_delta(int c, int p, int mod) {
  int d = (c - p) & (mod - 1);
  return d >= mod / 2 ? d - mod : d;
}

static inline int scale_round(int d, float f) {
  return (int)lrintf((float)d * f);
}

typedef struct ObjInfo { int x, y; uint16_t tile_attr; uint8_t size; } ObjInfo;

static void decode_oam(const Ppu *p, ObjInfo *o) {
  for (int i = 0; i < 128; i++) {
    uint16_t w0 = p->oam[i * 2], w1 = p->oam[i * 2 + 1];
    uint8_t hi = (p->highOam[i >> 2] >> ((i & 3) * 2)) & 3;
    o[i].x = (w0 & 0xff) | ((hi & 1) << 8);
    o[i].y = w0 >> 8;
    o[i].tile_attr = w1;
    o[i].size = hi >> 1;
  }
}

/* Writes the 9-bit X and 8-bit Y of slot i back into a PPU's OAM. */
static void encode_pos(Ppu *p, int i, int x, int y) {
  x &= 0x1ff;
  y &= 0xff;
  p->oam[i * 2] = (uint16_t)((x & 0xff) | (y << 8));
  uint8_t sh = (uint8_t)((i & 3) * 2);
  p->highOam[i >> 2] = (uint8_t)((p->highOam[i >> 2] & ~(1 << sh)) | (((x >> 8) & 1) << sh));
}

bool Srw1Interp_RenderSubframe(float f, uint8_t *out) {
  if (g_mode == kSrw1Motion_Off || g_cur < 0) return false;
  Capture *C = &g_cap[g_cur], *P = &g_cap[g_cur ^ 1];
  if (!C->complete || !P->complete || !P->start || C->serial != P->serial + 1)
    return false;
  const Ppu *cs = C->start;
  /* Skip while the picture is blanked or a different mode set is active
   * (scene transitions): nothing meaningful to blend. */
  if ((cs->inidisp & 0x80) || (cs->bgmode & 7) != (P->start->bgmode & 7))
    return false;
  if (!g_scratch) g_scratch = (Ppu *)malloc(sizeof(Ppu));
  if (!g_scratch) return false;
  Ppu *s = g_scratch;
  memcpy(s, cs, sizeof(Ppu));
  s->renderBuffer = out;
  s->renderPitch = 256 * 4;
  memset(&s->mode7Hd, 0, sizeof(s->mode7Hd));
  for (int i = 0; i < kPpuOverlaySource_Count; i++) s->overlayRenderBuffer[i] = NULL;
  s->widescreenLineEnhancer = NULL;
  s->widescreenLineEnhancerContext = NULL;

  /* Sprites: match each newest-tick OAM slot to its previous-tick position by
   * slot + tile/attributes, else by nearest identical tile; unmatched slots
   * (spawns, swaps, teleports) stay where the game put them. */
  ObjInfo co[128], po[128];
  decode_oam(cs, co);
  decode_oam(P->start, po);
  for (int i = 0; i < 128; i++) {
    int dx = 0, dy = 0;
    bool ok = false;
    if (po[i].tile_attr == co[i].tile_attr && po[i].size == co[i].size) {
      dx = wrap_delta(co[i].x, po[i].x, 512);
      dy = wrap_delta(co[i].y, po[i].y, 256);
      ok = abs(dx) <= kObjMaxDelta && abs(dy) <= kObjMaxDelta;
    }
    if (!ok) {
      int best = -1, bestd = kObjSearchRadius * 2 + 1;
      for (int j = 0; j < 128; j++) {
        if (po[j].tile_attr != co[i].tile_attr || po[j].size != co[i].size) continue;
        int ddx = wrap_delta(co[i].x, po[j].x, 512), ddy = wrap_delta(co[i].y, po[j].y, 256);
        if (abs(ddx) > kObjSearchRadius || abs(ddy) > kObjSearchRadius) continue;
        int d = abs(ddx) + abs(ddy);
        if (d < bestd) { bestd = d; best = j; dx = ddx; dy = ddy; }
        else if (d == bestd) best = -2; /* ambiguous */
      }
      ok = best >= 0;
    }
    if (!ok || (dx == 0 && dy == 0)) continue;
    /* Keep parked/off-screen sprites parked (SMW hides OBJ at Y=$F0). */
    if (co[i].y >= 0xE0 && co[i].y < 0xF8) continue;
    encode_pos(s, i, co[i].x + scale_round(dx, f), co[i].y + scale_round(dy, f));
  }

  /* Backgrounds: interpolate each line's scroll registers independently so
   * parallax, HDMA waves and the IRQ-split status bar each keep their own
   * motion. Mode 7 lines are left untouched. */
  for (int line = 0; line < kLines; line++) {
    memcpy(s, C->regs[line], kRegBytes);
    memcpy(s->cgram, C->cgram[line], sizeof(s->cgram));
    const Ppu *cl = (const Ppu *)C->regs[line];
    const Ppu *pl = (const Ppu *)P->regs[line];
    if ((cl->bgmode & 7) != 7 && (cl->bgmode & 7) == (pl->bgmode & 7)) {
      for (int l = 0; l < 4; l++) {
        int dx = wrap_delta(cl->hScroll[l], pl->hScroll[l], 1024);
        int dy = wrap_delta(cl->vScroll[l], pl->vScroll[l], 1024);
        if (abs(dx) > kBgMaxDelta || abs(dy) > kBgMaxDelta) continue;
        s->hScroll[l] = (uint16_t)((cl->hScroll[l] + scale_round(dx, f)) & 0x3ff);
        s->vScroll[l] = (uint16_t)((cl->vScroll[l] + scale_round(dy, f)) & 0x3ff);
      }
    }
    ppu_drawCapturedLine(s, line, true);
  }
  return true;
}
