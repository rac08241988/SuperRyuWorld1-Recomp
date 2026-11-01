/* Super Ryu World 1: in-game options menu (F12 / gamepad Guide).
 * Hosts the RetroArch .glslp shader-preset picker. Presets are scanned from
 * shaders/presets/ beside the executable; the choice persists in
 * srw1_options.ini and is selectable only from this menu. */
#ifndef SRW1_OPTIONS_H
#define SRW1_OPTIONS_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void Srw1Options_Init(void);
/* Preset path to load when the OpenGL renderer starts, or NULL. */
const char *Srw1Options_StartupShader(void);
bool Srw1Options_IsOpen(void);
/* Options menu "Side art" (default On). */
bool Srw1Options_SideArt(void);
void Srw1Options_Toggle(void);
/* Returns true when the event belongs to the menu (keep it from the game). */
bool Srw1Options_HandleKey(int sdl_keycode, bool pressed, bool repeat);
bool Srw1Options_HandleButton(int sdl_gamepad_button, bool pressed);
/* Called per presented frame: keeps a copy of the clean game frame and
 * composites the menu over it while open. */
void Srw1Options_OnPresent(uint8_t *pixels, int pitch, int width, int height);
/* While paused in the menu: restore the last clean frame, then overlay. */
void Srw1Options_RedrawPaused(uint8_t *pixels, int pitch, int width, int height);
/* Applies a pending (debounced) shader change; call once per loop. */
void Srw1Options_Tick(void);
/* Implemented by main.c: [General] BackgroundInput in config.ini. */
bool Srw1_GetBackgroundInput(void);
void Srw1_SetBackgroundInput(bool enabled);
/* Implemented by opengl.c under SMW_SRW1_BUILD. */
bool OpenGLRenderer_SetShader(const char *path);
bool OpenGLRenderer_IsActive(void);
#ifdef __cplusplus
}
#endif
/* The side panel as a window-resolution ARGB layer, or false when closed.
 * The renderer blends argb (w*h, top-down) at (x, y) in drawable pixels. */
bool Srw1Options_Layer(int out_w, int out_h, int vp_x, int vp_y, int vp_w, int vp_h,
                       const uint32_t **argb, int *x, int *y, int *w, int *h);

#endif
