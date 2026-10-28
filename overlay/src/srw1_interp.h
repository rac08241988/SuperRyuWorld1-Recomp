/* Super Ryu World 1: render-side motion interpolation for 120 Hz+ displays.
 * Game logic stays at 60 Hz; only presentation gains in-between frames built
 * by re-rasterizing the captured PPU state with interpolated BG scroll and
 * OAM positions. Off is the untouched original path. */
#ifndef SRW1_INTERP_H
#define SRW1_INTERP_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
  kSrw1Motion_Off = 0,
  kSrw1Motion_Interpolate = 1,  /* in-betweens toward the newest tick */
  kSrw1Motion_Extrapolate = 2,  /* low-latency: project past the newest tick */
} Srw1MotionMode;

struct Ppu;
Srw1MotionMode Srw1Interp_GetMode(void);
void Srw1Interp_SetMode(Srw1MotionMode mode);
/* Called by the live frame raster before each ppu_runLine(line). */
void Srw1Interp_CaptureLine(struct Ppu *ppu, int line);
/* Called after the live raster finishes the frame. */
void Srw1Interp_EndFrame(void);
/* f = signed fraction of the last tick's motion to apply relative to the
 * newest captured frame (-1..0 interpolate, 0..1 extrapolate). Writes a
 * 256x224 XRGB frame with a 1024-byte pitch. False = not available. */
bool Srw1Interp_RenderSubframe(float f, uint8_t *out);
#ifdef __cplusplus
}
#endif
#endif
