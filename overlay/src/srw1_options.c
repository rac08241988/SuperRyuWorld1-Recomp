/* Super Ryu World 1: in-game options menu with the RetroArch shader preset
 * picker. See srw1_options.h. */
#include "srw1_options.h"
#include "recomp_runtime_ui.h"
#include "srw1_interp.h"
#include "desktop/sdl_compat.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SRW1_PRESET_ROOT "shaders/presets"
#define SRW1_OPTIONS_FILE "srw1_options.ini"
#define SRW1_MAX_PRESETS 512

static const char *const kMotionLabels[] = {"Off", "Interpolate", "Low-latency"};
static const char *const kMotionKeys[] = {"off", "interpolate", "lowlatency"};

static const char *const kOnOffLabels[] = {"Off", "On"};
static bool g_side_art = true; /* stage art in the pillarbox bars */

static int parse_motion(const char *v) {
  for (int i = 0; i < 3; i++)
    if (strcmp(v, kMotionKeys[i]) == 0) return i;
  return -1;
}

static char *g_paths[SRW1_MAX_PRESETS + 1];   /* [0] = Off (NULL) */
static char *g_labels[SRW1_MAX_PRESETS + 1];
static size_t g_count;                          /* including Off */
static int g_current;                           /* applied index */
static int g_pending = -1;                      /* chosen, not yet applied */
static uint64_t g_pending_at;
static RecompRuntimeUi *g_ui;
static char g_status[160];
static uint8_t *g_frame_copy;
static size_t g_frame_copy_size;
static bool g_initialized;

static int cmp_str(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static void scan_dir(const char *dir, char **found, size_t *n) {
  DIR *d = opendir(dir);
  if (!d) return;
  struct dirent *e;
  while ((e = readdir(d)) != NULL && *n < SRW1_MAX_PRESETS) {
    if (e->d_name[0] == '.') continue;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
    struct stat st;
    if (stat(path, &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      scan_dir(path, found, n);
    } else {
      size_t len = strlen(path);
      if (len > 6 && strcmp(path + len - 6, ".glslp") == 0)
        found[(*n)++] = strdup(path);
    }
  }
  closedir(d);
}

static char *make_label(const char *path) {
  const char *rel = path + strlen(SRW1_PRESET_ROOT) + 1;
  size_t len = strlen(rel) - 6;
  char *s = (char *)malloc(len + 1);
  memcpy(s, rel, len);
  s[len] = 0;
  return s;
}

static void save_choice(void) {
  FILE *f = fopen(SRW1_OPTIONS_FILE, "w");
  if (!f) return;
  fprintf(f, "# Written by the in-game options menu (Esc / F12). Edit there.\n");
  fprintf(f, "ShaderPreset = %s\n", g_current > 0 ? g_paths[g_current] : "");
  fprintf(f, "MotionMode = %s\n", kMotionKeys[Srw1Interp_GetMode()]);
  fprintf(f, "SideArt = %s\n", g_side_art ? "on" : "off");
  fclose(f);
}

static void load_motion(void) {
  int m = -1, side = -1;
  FILE *f = fopen(SRW1_OPTIONS_FILE, "r");
  if (f) {
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      bool is_motion = strncmp(line, "MotionMode", 10) == 0;
      bool is_side = strncmp(line, "SideArt", 7) == 0;
      if (!is_motion && !is_side) continue;
      char *v = strchr(line, '=');
      if (!v) continue;
      v++;
      while (*v == ' ' || *v == '\t') v++;
      size_t l = strlen(v);
      while (l && (v[l - 1] == '\n' || v[l - 1] == '\r' || v[l - 1] == ' ')) v[--l] = 0;
      if (is_motion) m = parse_motion(v);
      else side = (strcmp(v, "off") == 0 || strcmp(v, "0") == 0) ? 0 : 1;
    }
    fclose(f);
  }
  const char *env = getenv("SRW1_MOTION"); /* test override */
  if (env && parse_motion(env) >= 0) m = parse_motion(env);
  const char *senv = getenv("SRW1_SIDE_ART"); /* test override: 0/1 */
  if (senv && senv[0]) side = senv[0] != '0';
  if (side >= 0) g_side_art = side != 0;
  Srw1Interp_SetSideArt(g_side_art);
  if (m >= 0) Srw1Interp_SetMode((Srw1MotionMode)m);
}

bool Srw1Options_SideArt(void) { return g_side_art; }

static int load_choice(void) {
  const char *env = getenv("SRW1_SHADER_PRESET"); /* test override */
  char want[1024] = {0};
  if (env) {
    snprintf(want, sizeof(want), "%s", env);
  } else {
    FILE *f = fopen(SRW1_OPTIONS_FILE, "r");
    if (!f) return 0;
    char line[1100];
    while (fgets(line, sizeof(line), f)) {
      if (strncmp(line, "ShaderPreset", 12) != 0) continue;
      char *v = strchr(line, '=');
      if (!v) continue;
      v++;
      while (*v == ' ' || *v == '\t') v++;
      size_t l = strlen(v);
      while (l && (v[l - 1] == '\n' || v[l - 1] == '\r' || v[l - 1] == ' ')) v[--l] = 0;
      snprintf(want, sizeof(want), "%s", v);
    }
    fclose(f);
  }
  if (!want[0]) return 0;
  for (size_t i = 1; i < g_count; i++)
    if (strcmp(g_paths[i], want) == 0) return (int)i;
  fprintf(stderr, "[srw1] shader preset '%s' not found under %s\n", want, SRW1_PRESET_ROOT);
  return 0;
}

static const RecompRuntimeUiItem *g_items;

enum { kItemShader, kItemMotion, kItemSideArt, kItemResume, kItemCount };


static int ui_get(void *ctx, const RecompRuntimeUiItem *item, int *out) {
  (void)ctx;
  if (item == &g_items[kItemShader]) {
    *out = g_pending >= 0 ? g_pending : g_current;
    return 1;
  }
  if (item == &g_items[kItemMotion]) {
    *out = (int)Srw1Interp_GetMode();
    return 1;
  }
  if (item == &g_items[kItemSideArt]) {
    *out = g_side_art ? 1 : 0;
    return 1;
  }
  return 0;
}

static int ui_set(void *ctx, const RecompRuntimeUiItem *item, int value) {
  (void)ctx;
  if (item == &g_items[kItemMotion]) {
    if (value < 0 || value > 2) return 0;
    Srw1Interp_SetMode((Srw1MotionMode)value);
    save_choice();
    snprintf(g_status, sizeof(g_status), "Motion: %s", kMotionLabels[value]);
    recomp_runtime_ui_set_status(g_ui, g_status);
    return 1;
  }
  if (item == &g_items[kItemSideArt]) {
    if (value < 0 || value > 1) return 0;
    g_side_art = value != 0;
    Srw1Interp_SetSideArt(g_side_art);
    save_choice();
    snprintf(g_status, sizeof(g_status), "Side art: %s", kOnOffLabels[value]);
    recomp_runtime_ui_set_status(g_ui, g_status);
    return 1;
  }
  if (item != &g_items[kItemShader]) return 0;
  if (value < 0 || (size_t)value >= g_count) return 0;
  g_pending = value;
  g_pending_at = SDL_GetTicks();
  return 1;
}

static int ui_action(void *ctx, const RecompRuntimeUiItem *item) {
  (void)ctx;
  if (item == &g_items[kItemResume]) {
    recomp_runtime_ui_close(g_ui);
    return 1;
  }
  return 0;
}

static int ui_enabled(void *ctx, const RecompRuntimeUiItem *item) {
  (void)ctx;
  if (item == &g_items[kItemShader]) return OpenGLRenderer_IsActive() && g_count > 1;
  return 1;
}

void Srw1Options_Init(void) {
  if (g_initialized) return;
  g_initialized = true;
  char *found[SRW1_MAX_PRESETS];
  size_t n = 0;
  scan_dir(SRW1_PRESET_ROOT, found, &n);
  qsort(found, n, sizeof(found[0]), cmp_str);
  g_paths[0] = NULL;
  g_labels[0] = strdup("Off");
  for (size_t i = 0; i < n; i++) {
    g_paths[i + 1] = found[i];
    g_labels[i + 1] = make_label(found[i]);
  }
  g_count = n + 1;
  g_current = load_choice();
  load_motion();
  fprintf(stderr, "[srw1] %zu shader presets under %s; startup=%s\n", n,
          SRW1_PRESET_ROOT, g_current ? g_labels[g_current] : "Off");

  static RecompRuntimeUiItem items[kItemCount];
  memset(items, 0, sizeof(items));
  items[kItemShader].key = "graphics.shader_preset";
  items[kItemShader].section = "Video";
  items[kItemShader].label = "Shader";
  items[kItemShader].description =
      "RetroArch GLSL preset (shaders/presets). Needs the OpenGL output.";
  items[kItemShader].type = RECOMP_RUNTIME_UI_CHOICE;
  items[kItemShader].minimum = 0;
  items[kItemShader].maximum = (int)g_count - 1;
  items[kItemShader].step = 1;
  items[kItemShader].choices = (const char *const *)g_labels;
  items[kItemShader].choice_count = g_count;
  items[kItemMotion].key = "graphics.motion";
  items[kItemMotion].section = "Video";
  items[kItemMotion].label = "120Hz motion";
  items[kItemMotion].description =
      "High-refresh presentation (game logic stays 60 Hz). Off = original.";
  items[kItemMotion].type = RECOMP_RUNTIME_UI_CHOICE;
  items[kItemMotion].minimum = 0;
  items[kItemMotion].maximum = 2;
  items[kItemMotion].step = 1;
  items[kItemMotion].choices = kMotionLabels;
  items[kItemMotion].choice_count = 3;
  items[kItemSideArt].key = "graphics.side_art";
  items[kItemSideArt].section = "Video";
  items[kItemSideArt].label = "Side art";
  items[kItemSideArt].description =
      "Fill the side bars with the stage's own background. Off = black bars.";
  items[kItemSideArt].type = RECOMP_RUNTIME_UI_CHOICE;
  items[kItemSideArt].minimum = 0;
  items[kItemSideArt].maximum = 1;
  items[kItemSideArt].step = 1;
  items[kItemSideArt].choices = kOnOffLabels;
  items[kItemSideArt].choice_count = 2;
  items[kItemResume].key = RECOMP_RUNTIME_UI_KEY_RESUME;
  items[kItemResume].section = "Video";
  items[kItemResume].label = "Resume";
  items[kItemResume].type = RECOMP_RUNTIME_UI_ACTION;
  g_items = items;

  static RecompRuntimeUiConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.title = "Options";
  cfg.subtitle = "Super Ryu World 1";
  cfg.items = items;
  cfg.item_count = kItemCount;
  cfg.theme = "snes";
  cfg.callbacks.get_value = ui_get;
  cfg.callbacks.set_value = ui_set;
  cfg.callbacks.run_action = ui_action;
  cfg.callbacks.is_enabled = ui_enabled;
  g_ui = recomp_runtime_ui_create(&cfg);
}

const char *Srw1Options_StartupShader(void) {
  Srw1Options_Init();
  return g_current > 0 ? g_paths[g_current] : NULL;
}

bool Srw1Options_IsOpen(void) { return g_ui && recomp_runtime_ui_is_open(g_ui); }

void Srw1Options_Toggle(void) {
  if (!g_ui) return;
  if (recomp_runtime_ui_is_open(g_ui)) recomp_runtime_ui_close(g_ui);
  else recomp_runtime_ui_open(g_ui);
}

static bool feed(RecompRuntimeUiInput in, bool pressed, bool repeat) {
  return recomp_runtime_ui_handle_input(g_ui, in, pressed, repeat) != 0;
}

bool Srw1Options_HandleKey(int key, bool pressed, bool repeat) {
  Srw1Options_Init();
  if (!g_ui) return false;
  /* F12 and Escape both open/close the menu (Escape never quits). */
  if (key == SDLK_F12 || key == SDLK_ESCAPE) {
    if (pressed && !repeat) Srw1Options_Toggle();
    return true;
  }
  if (!recomp_runtime_ui_is_open(g_ui)) return false;
  switch (key) {
  case SDLK_UP: return feed(RECOMP_RUNTIME_UI_INPUT_UP, pressed, repeat);
  case SDLK_DOWN: return feed(RECOMP_RUNTIME_UI_INPUT_DOWN, pressed, repeat);
  case SDLK_LEFT: return feed(RECOMP_RUNTIME_UI_INPUT_LEFT, pressed, repeat);
  case SDLK_RIGHT: return feed(RECOMP_RUNTIME_UI_INPUT_RIGHT, pressed, repeat);
  case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
    return feed(RECOMP_RUNTIME_UI_INPUT_ACCEPT, pressed, repeat);
  case SDLK_BACKSPACE:
    return feed(RECOMP_RUNTIME_UI_INPUT_BACK, pressed, repeat);
  default: return true; /* swallow everything else while open */
  }
}

bool Srw1Options_HandleButton(int b, bool pressed) {
  Srw1Options_Init();
  if (!g_ui) return false;
  if (b == SDL_CONTROLLER_BUTTON_GUIDE) {
    if (pressed) Srw1Options_Toggle();
    return true;
  }
  if (!recomp_runtime_ui_is_open(g_ui)) return false;
  switch (b) {
  case SDL_CONTROLLER_BUTTON_DPAD_UP: return feed(RECOMP_RUNTIME_UI_INPUT_UP, pressed, false);
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return feed(RECOMP_RUNTIME_UI_INPUT_DOWN, pressed, false);
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return feed(RECOMP_RUNTIME_UI_INPUT_LEFT, pressed, false);
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return feed(RECOMP_RUNTIME_UI_INPUT_RIGHT, pressed, false);
  case SDL_CONTROLLER_BUTTON_A: return feed(RECOMP_RUNTIME_UI_INPUT_ACCEPT, pressed, false);
  case SDL_CONTROLLER_BUTTON_B: return feed(RECOMP_RUNTIME_UI_INPUT_BACK, pressed, false);
  case SDL_CONTROLLER_BUTTON_START: return feed(RECOMP_RUNTIME_UI_INPUT_TOGGLE, pressed, false);
  default: return true;
  }
}

static void apply_pending(void) {
  int want = g_pending;
  g_pending = -1;
  if (want < 0 || want == g_current) return;
  if (OpenGLRenderer_SetShader(want > 0 ? g_paths[want] : NULL)) {
    g_current = want;
    save_choice();
    snprintf(g_status, sizeof(g_status), "Shader: %s", g_labels[want]);
  } else {
    snprintf(g_status, sizeof(g_status), "Failed: %s", g_labels[want]);
    fprintf(stderr, "[srw1] shader preset failed to load: %s\n",
            want > 0 ? g_paths[want] : "(off)");
  }
  recomp_runtime_ui_set_status(g_ui, g_status);
}

void Srw1Options_Tick(void) {
  Srw1Options_Init();
  if (g_pending >= 0 && SDL_GetTicks() - g_pending_at >= 300) apply_pending();
}

static void keep_copy(const uint8_t *pixels, int pitch, int width, int height) {
  size_t need = (size_t)width * 4 * height;
  if (need > g_frame_copy_size) {
    free(g_frame_copy);
    g_frame_copy = (uint8_t *)malloc(need);
    g_frame_copy_size = g_frame_copy ? need : 0;
  }
  if (!g_frame_copy) return;
  for (int y = 0; y < height; y++)
    memcpy(g_frame_copy + (size_t)y * width * 4, pixels + (size_t)y * pitch, (size_t)width * 4);
}

void Srw1Options_OnPresent(uint8_t *pixels, int pitch, int width, int height) {
  keep_copy(pixels, pitch, width, height);
  if (Srw1Options_IsOpen())
    recomp_runtime_ui_render_argb8888(g_ui, pixels, width, height, pitch);
}

void Srw1Options_RedrawPaused(uint8_t *pixels, int pitch, int width, int height) {
  if (g_frame_copy && g_frame_copy_size >= (size_t)width * 4 * height)
    for (int y = 0; y < height; y++)
      memcpy(pixels + (size_t)y * pitch, g_frame_copy + (size_t)y * width * 4, (size_t)width * 4);
  if (Srw1Options_IsOpen())
    recomp_runtime_ui_render_argb8888(g_ui, pixels, width, height, pitch);
}
