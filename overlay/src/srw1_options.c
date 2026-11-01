/* Super Ryu World 1: the in-game Options side panel (F12 / Esc / gamepad
 * Guide). See srw1_options.h.
 *
 * v1.1.0: a frosted side panel drawn at window resolution over the paused
 * game (same kit and behaviour as Demon's Crest v1.1.0): tabs Video /
 * Display / Controls / Shaders / System, edits stay pending until Apply,
 * shader presets preview live and commit on Apply. Input is read here ONLY
 * while the panel is open; closed, every key and button goes to the game
 * exactly as before. */
#include "srw1_options.h"
#include "rpanel.h"
#include <math.h>
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


/* ---- host hooks (main.c, SRW1 build) ------------------------------------- */
int Srw1Host_GetFullscreen(void);
void Srw1Host_SetFullscreen(int on);
int Srw1Host_GetWindowScale(void);
void Srw1Host_SetWindowScale(int scale);
int Srw1Host_GetLinear(void);
void Srw1Host_SetLinear(int on);
int Srw1Host_GetStretch(void);
void Srw1Host_SetStretch(int on);
int Srw1Host_GetSkipLauncher(void);
void Srw1Host_SetSkipLauncher(int on);
void Srw1Host_SaveState(int slot);   /* 0-based */
void Srw1Host_LoadState(int slot);
void Srw1Host_Reset(void);
void RtlSaveSlotPath(int slot, char *buf, size_t buflen);

#define SRW1_STATE_SLOTS 10
#define SRW1_SHADER_NOTE "Shaders only work on the OpenGL renderer (OutputMethod = OpenGL)."
static int g_slot = 1;

typedef struct SnesModalLayerView {
  int out_w, out_h;
  int vp_x, vp_y, vp_w, vp_h;
  const uint32_t *frame;
  int frame_w, frame_h;
} SnesModalLayerView;

/* ---- shader apply / preview --------------------------------------------- */

static double g_status_at;
static int g_dirty = 1;
static int g_preview = -1;   /* shown live behind the panel, not saved */

static double now_s(void) { return (double)SDL_GetTicks() / 1000.0; }

static void status(const char *fmt, const char *a, const char *b) {
  snprintf(g_status, sizeof(g_status), fmt, a ? a : "", b ? b : "");
  g_status_at = now_s();
  g_dirty = 1;
  fprintf(stderr, "[srw1] options: %s\n", g_status);
}

static bool shader_live_ok(int i) { return i <= 0 || OpenGLRenderer_IsActive(); }

static bool run_shader(int want) {
  if (want < 0 || (size_t)want >= g_count) return false;
  if (!OpenGLRenderer_IsActive()) return want == 0;
  if (OpenGLRenderer_SetShader(want > 0 ? g_paths[want] : NULL)) return true;
  fprintf(stderr, "[srw1] shader preset failed to load: %s\n", want > 0 ? g_paths[want] : "(off)");
  return false;
}

static void apply_shader(int want) {
  if (want < 0 || (size_t)want >= g_count) return;
  if (want > 0 && !shader_live_ok(want)) {
    status("Shaders need the OpenGL renderer (OutputMethod = OpenGL)%s%s", "", "");
    return;
  }
  if (run_shader(want)) {
    g_current = want;
    g_preview = want;
    save_choice();
    status("Shader: %s%s", g_labels[want], "");
  } else {
    status("Failed: %s%s", g_labels[want], "");
    run_shader(g_current);
    g_preview = g_current;
  }
}

static void preview_shader(int want) {
  if (want == g_preview) return;
  if (!shader_live_ok(want)) {
    status("%s needs the OpenGL renderer%s", g_labels[want], "");
    return;
  }
  if (run_shader(want)) {
    g_preview = want;
    if (want > 0) status("Previewing %s%s", g_labels[want], "");
  } else {
    status("Failed: %s%s", g_labels[want], "");
  }
}

/* ---- rows ---------------------------------------------------------------- */

enum { T_VIDEO, T_DISPLAY, T_CONTROLS, T_SHADERS, T_SYSTEM, T_COUNT };
static const char *const kTabNames[T_COUNT] = {"Video", "Display", "Controls", "Shaders", "System"};
typedef enum { R_PICK, R_TOGGLE, R_SLIDER, R_ACTION, R_SHADER, R_NOTE } RowType;
typedef struct Row {
  const char *key, *label, *desc;
  RowType type;
  int tab;
  const char *const *choices;
  int nchoices;
  const int *values;
  int min, max, step;
  const char *unit;
  int cur, pend;
} Row;
#define DC_MAX_ROWS 32
static Row g_rows[DC_MAX_ROWS];
static int g_nrows;
static const char *const kOnOff[] = {"Off", "On"};
static const char *const kFullscreenLabels[] = {"Windowed", "Fullscreen"};

static Row *row(const char *key, int tab, const char *label, const char *desc, RowType t) {
  Row *r = &g_rows[g_nrows++];
  memset(r, 0, sizeof(*r));
  r->key = key; r->tab = tab; r->label = label; r->desc = desc; r->type = t;
  r->step = 1;
  return r;
}
static void pick(Row *r, const char *const *labels, int n, const int *values, int min) {
  r->choices = labels; r->nchoices = n; r->values = values; r->min = min;
  r->max = min + n - 1;
}
static bool key_is(const Row *r, const char *k) { return r->key && strcmp(r->key, k) == 0; }
static int pick_index(const Row *r, int v) {
  if (r->values) {
    for (int i = 0; i < r->nchoices; i++) if (r->values[i] == v) return i;
    return 0;
  }
  int i = v - r->min;
  return i < 0 ? 0 : i >= r->nchoices ? r->nchoices - 1 : i;
}
static int pick_value(const Row *r, int i) { return r->values ? r->values[i] : r->min + i; }

static void build_rows(void) {
  Row *r;
  g_nrows = 0;
  r = row("graphics.motion", T_VIDEO, "120Hz motion",
          "120 Hz output with 60 Hz game logic. Needs a 100 Hz+ display.", R_PICK);
  pick(r, kMotionLabels, 3, NULL, 0);
  row("graphics.side_art", T_VIDEO, "Side art",
      "Fill the pillarbox bars with the stage's own background.", R_TOGGLE);
  row("display.linear", T_VIDEO, "Linear filter",
      "Smooth scaling. Off keeps sharp pixels.", R_TOGGLE);
  r = row("display.fullscreen", T_DISPLAY, "Fullscreen", "Window mode (also Alt+Enter).", R_PICK);
  pick(r, kFullscreenLabels, 2, NULL, 0);
  r = row("display.scale", T_DISPLAY, "Window scale",
          "Window size in SNES pixels (also Ctrl+Up/Down).", R_SLIDER);
  r->min = 1; r->max = 10; r->unit = "x";
  row("display.stretch", T_DISPLAY, "Stretch to window",
      "Ignore the aspect ratio and fill the window.", R_TOGGLE);
  row("controls.background", T_CONTROLS, "Background input",
      "Off: controller input is ignored while the window is not focused.", R_TOGGLE);
  row("graphics.shader_preset", T_SHADERS, "Shader",
      "Choose a preset: it previews live behind this panel, Apply keeps it.", R_SHADER);
  row(NULL, T_SHADERS, SRW1_SHADER_NOTE, NULL, R_NOTE);
  r = row("system.slot", T_SYSTEM, "Save state slot", "The slot Save and Load use.", R_SLIDER);
  r->min = 1; r->max = SRW1_STATE_SLOTS; r->unit = "";
  row("system.save", T_SYSTEM, "Save state", "Save this moment to the selected slot.", R_ACTION);
  row("system.load", T_SYSTEM, "Load state", "Return to the state in the selected slot.", R_ACTION);
  row("system.skip_launcher", T_SYSTEM, "Skip launcher at startup",
      "Start straight into the game.", R_TOGGLE);
  row("system.resume", T_SYSTEM, "Resume", "Close this panel.", R_ACTION);
  /* No "Reset console" row: the host soft reset (Ctrl+R) overflows the stack
   * in this build, and the panel never offers what the game cannot do. */
  row("system.quit", T_SYSTEM, "Quit", "Close Super Ryu World 1.", R_ACTION);
}

static int read_value(const Row *r) {
  if (key_is(r, "graphics.motion")) return (int)Srw1Interp_GetMode();
  if (key_is(r, "graphics.side_art")) return g_side_art ? 1 : 0;
  if (key_is(r, "graphics.shader_preset")) return g_current;
  if (key_is(r, "controls.background")) return Srw1_GetBackgroundInput() ? 1 : 0;
  if (key_is(r, "system.slot")) return g_slot;
  if (key_is(r, "display.linear")) return Srw1Host_GetLinear();
  if (key_is(r, "display.fullscreen")) return Srw1Host_GetFullscreen();
  if (key_is(r, "display.scale")) return Srw1Host_GetWindowScale();
  if (key_is(r, "display.stretch")) return Srw1Host_GetStretch();
  if (key_is(r, "system.skip_launcher")) return Srw1Host_GetSkipLauncher();
  return 0;
}

static void seed_rows(void) {
  for (int i = 0; i < g_nrows; i++) {
    g_rows[i].cur = read_value(&g_rows[i]);
    g_rows[i].pend = g_rows[i].cur;
  }
}

static int pending_count(void) {
  int n = 0;
  for (int i = 0; i < g_nrows; i++)
    if (g_rows[i].type != R_ACTION && g_rows[i].type != R_NOTE && g_rows[i].pend != g_rows[i].cur) n++;
  return n;
}

static void close_menu(bool revert);

static void apply_all(void) {
  int changed = pending_count();
  if (!changed) { status("Nothing to apply%s%s", "", ""); return; }
  int shader_want = -1;
  for (int i = 0; i < g_nrows; i++) {
    Row *r = &g_rows[i];
    if (r->type == R_ACTION || r->type == R_NOTE || r->pend == r->cur) continue;
    int v = r->pend;
    if (key_is(r, "graphics.shader_preset")) { shader_want = v; continue; }
    if (key_is(r, "graphics.motion")) {
      Srw1Interp_SetMode((Srw1MotionMode)v);
      save_choice();
    } else if (key_is(r, "graphics.side_art")) {
      g_side_art = v != 0;
      Srw1Interp_SetSideArt(g_side_art);
      save_choice();
    } else if (key_is(r, "controls.background")) {
      Srw1_SetBackgroundInput(v != 0);
    } else if (key_is(r, "system.slot")) {
      g_slot = v;
    } else if (key_is(r, "display.linear")) {
      Srw1Host_SetLinear(v);
    } else if (key_is(r, "display.fullscreen")) {
      Srw1Host_SetFullscreen(v);
    } else if (key_is(r, "display.scale")) {
      Srw1Host_SetWindowScale(v);
    } else if (key_is(r, "display.stretch")) {
      Srw1Host_SetStretch(v);
    } else if (key_is(r, "system.skip_launcher")) {
      Srw1Host_SetSkipLauncher(v);
    }
  }
  if (shader_want >= 0) apply_shader(shader_want);
  seed_rows();
  if (shader_want < 0 || shader_want == g_current) {
    char n[16];
    snprintf(n, sizeof(n), "%d", changed);
    status("Applied %s change%s", n, changed == 1 ? "" : "s");
  }
}

/* ---- the panel ----------------------------------------------------------- */

static bool g_open;
static int g_tab;
static int g_focus;
static bool g_list;
static int g_list_sel, g_list_top;
static double g_list_moved_at;
static int g_confirm_row = -1;
static double g_confirm_at;
static int g_last_pad;
static float g_hl_y = -1, g_tab_x = -1, g_tab_w;
static double g_opened_at;

static int tab_rows(int tab, int *idx) {
  int n = 0;
  for (int i = 0; i < g_nrows; i++)
    if (g_rows[i].tab == tab && g_rows[i].type != R_NOTE) { if (idx) idx[n] = i; n++; }
  return n;
}
static Row *focused_row(void) {
  int idx[DC_MAX_ROWS];
  int n = tab_rows(g_tab, idx);
  return g_focus < n ? &g_rows[idx[g_focus]] : NULL;
}

static void open_menu(void) {
  if (g_open) return;
  g_open = true;
  g_list = false;
  g_confirm_row = -1;
  seed_rows();
  g_preview = g_current;
  g_hl_y = -1; g_tab_x = -1;
  g_status[0] = 0;
  g_opened_at = now_s();
  g_dirty = 1;
  fprintf(stderr, "[srw1] options menu opened\n");
}

static void close_menu(bool revert) {
  if (!g_open) return;
  g_open = false;
  g_list = false;
  if (revert && g_preview != g_current) {
    run_shader(g_current);
    g_preview = g_current;
  }
  fprintf(stderr, "[srw1] options menu closed\n");
}

static int shader_row_index(void) {
  for (int i = 0; i < g_nrows; i++) if (key_is(&g_rows[i], "graphics.shader_preset")) return i;
  return -1;
}

static void run_action(Row *r) {
  char n[16];
  snprintf(n, sizeof(n), "%d", g_slot);
  int ri = (int)(r - g_rows);
  if (key_is(r, "system.resume")) { close_menu(true); return; }
  if (key_is(r, "system.save")) {
    Srw1Host_SaveState(g_slot - 1);
    status("Saved state to slot %s%s", n, "");
    return;
  }
  if (key_is(r, "system.load")) {
    char path[512];
    struct stat st;
    RtlSaveSlotPath(g_slot - 1, path, sizeof(path));
    if (stat(path, &st) != 0) { status("Slot %s is empty%s", n, ""); return; }
    Srw1Host_LoadState(g_slot - 1);
    fprintf(stderr, "[srw1] options: loaded state slot %d\n", g_slot);
    close_menu(true);
    return;
  }
  if (key_is(r, "system.reset") || key_is(r, "system.quit")) {
    bool quit = key_is(r, "system.quit");
    if (g_confirm_row != ri || now_s() - g_confirm_at > 3.0) {
      g_confirm_row = ri;
      g_confirm_at = now_s();
      status(quit ? "Press again to quit%s%s" : "Press again to reset the console%s%s", "", "");
      return;
    }
    g_confirm_row = -1;
    close_menu(true);
    if (quit) {
      SDL_Event e;
      memset(&e, 0, sizeof(e));
#if SNESRECOMP_SDL3
      e.type = SDL_EVENT_QUIT;
#else
      e.type = SDL_QUIT;
#endif
      SDL_PushEvent(&e);
    } else {
      Srw1Host_Reset();
    }
  }
}

static void step_value(Row *r, int dir, bool wrap) {
  if (r->type == R_TOGGLE) { r->pend = !r->pend; return; }
  if (r->type == R_PICK) {
    int i = pick_index(r, r->pend) + dir;
    if (i < 0) i = wrap ? r->nchoices - 1 : 0;
    if (i >= r->nchoices) i = wrap ? 0 : r->nchoices - 1;
    r->pend = pick_value(r, i);
    return;
  }
  if (r->type == R_SLIDER) {
    int v = r->pend + dir * r->step;
    if (v < r->min) v = r->min;
    if (v > r->max) v = r->max;
    r->pend = v;
    return;
  }
  if (r->type == R_SHADER) {
    int v = r->pend + dir;
    if (v < 0) v = (int)g_count - 1;
    if (v >= (int)g_count) v = 0;
    r->pend = v;
    preview_shader(v);
  }
}
typedef enum { IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_OK, IN_BACK, IN_APPLY,
               IN_TAB_PREV, IN_TAB_NEXT, IN_PAGE_UP, IN_PAGE_DOWN } MenuIn;

static void list_move(int d) {
  int v = g_list_sel + d;
  if (v < 0) v = 0;
  if (v >= (int)g_count) v = (int)g_count - 1;
  if (v != g_list_sel) { g_list_sel = v; g_list_moved_at = now_s(); }
}

static void menu_input(MenuIn in) {
  g_dirty = 1;
  if (g_list) {
    int si = shader_row_index();
    switch (in) {
    case IN_UP: list_move(-1); break;
    case IN_DOWN: list_move(1); break;
    case IN_LEFT: case IN_PAGE_UP: case IN_TAB_PREV: list_move(-10); break;
    case IN_RIGHT: case IN_PAGE_DOWN: case IN_TAB_NEXT: list_move(10); break;
    case IN_OK:
      if (si >= 0) g_rows[si].pend = g_list_sel;
      preview_shader(g_list_sel);
      g_list = false;
      break;
    case IN_BACK:
      g_list = false;
      if (si >= 0 && shader_live_ok(g_rows[si].pend)) preview_shader(g_rows[si].pend);
      break;
    case IN_APPLY:
      if (si >= 0) g_rows[si].pend = g_list_sel;
      g_list = false;
      apply_all();
      break;
    }
    return;
  }
  int n = tab_rows(g_tab, NULL);
  Row *r = focused_row();
  switch (in) {
  case IN_UP: g_focus = g_focus > 0 ? g_focus - 1 : n; break;
  case IN_DOWN: g_focus = g_focus < n ? g_focus + 1 : 0; break;
  case IN_TAB_PREV: g_tab = (g_tab + T_COUNT - 1) % T_COUNT; g_focus = 0; break;
  case IN_TAB_NEXT: g_tab = (g_tab + 1) % T_COUNT; g_focus = 0; break;
  case IN_PAGE_UP: g_focus = 0; break;
  case IN_PAGE_DOWN: g_focus = n; break;
  case IN_LEFT: if (r) step_value(r, -1, false); break;
  case IN_RIGHT: if (r) step_value(r, 1, false); break;
  case IN_OK:
    if (!r) { apply_all(); break; }
    if (r->type == R_ACTION) run_action(r);
    else if (r->type == R_SHADER) {
      g_list = true;
      g_list_sel = r->pend;
      g_list_top = -1;
      g_list_moved_at = 0;
    } else step_value(r, 1, true);
    break;
  case IN_BACK: close_menu(true); break;
  case IN_APPLY: apply_all(); break;
  }
}

/* ---- drawing ------------------------------------------------------------- */

/* Super Ryu World: dragon red-orange with a gold edge. */
#define C_TEXT    0xFFFFF4EEu
#define C_DIM     0xFFC9B6A8u
#define C_FAINT   0x66FFFFFFu
#define C_ACCENT  0xFFE5512Bu
#define C_ACCENT2 0xFFF2B33Au
#define C_TINT    0xFF1C0D07u

static uint32_t *g_layer;
static size_t g_layer_cap;
static int g_lw, g_lh, g_lx, g_ly;
static int g_last_out_w, g_last_out_h;
static uint32_t g_blur[64 * 64];
static int g_blur_w, g_blur_h;
static const uint32_t *g_blur_src;

static void make_blur(const SnesModalLayerView *v) {
  g_blur_w = g_blur_h = 0;
  if (!v->frame || v->frame_w <= 0 || v->frame_h <= 0) return;
  int bw = 48, bh = 42;
  float acc[48 * 42][3];
  for (int y = 0; y < bh; y++)
    for (int x = 0; x < bw; x++) {
      int x0 = x * v->frame_w / bw, x1 = (x + 1) * v->frame_w / bw;
      int y0 = y * v->frame_h / bh, y1 = (y + 1) * v->frame_h / bh;
      float r = 0, g = 0, b = 0;
      int n = 0;
      for (int yy = y0; yy < y1; yy += 2)
        for (int xx = x0; xx < x1; xx += 2) {
          uint32_t p = v->frame[(size_t)yy * v->frame_w + xx];
          r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; n++;
        }
      if (!n) n = 1;
      acc[y * bw + x][0] = r / n; acc[y * bw + x][1] = g / n; acc[y * bw + x][2] = b / n;
    }
  for (int pass = 0; pass < 2; pass++) {
    static float tmp[48 * 42][3];
    for (int y = 0; y < bh; y++)
      for (int x = 0; x < bw; x++) {
        float s[3] = {0, 0, 0};
        int n = 0;
        for (int dy = -2; dy <= 2; dy++)
          for (int dx = -2; dx <= 2; dx++) {
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= bw || yy >= bh) continue;
            for (int k = 0; k < 3; k++) s[k] += acc[yy * bw + xx][k];
            n++;
          }
        for (int k = 0; k < 3; k++) tmp[y * bw + x][k] = s[k] / n;
      }
    memcpy(acc, tmp, sizeof(tmp));
  }
  for (int i = 0; i < bw * bh; i++)
    g_blur[i] = 0xFF000000u | ((uint32_t)acc[i][0] << 16) | ((uint32_t)acc[i][1] << 8) |
                (uint32_t)acc[i][2];
  g_blur_w = bw;
  g_blur_h = bh;
}

static uint32_t blur_at(const SnesModalLayerView *v, float sx, float sy) {
  if (!g_blur_w || v->vp_w <= 0 || v->vp_h <= 0) return 0xFF000000u;
  float u = (sx - v->vp_x) / v->vp_w * g_blur_w - .5f;
  float t = (sy - v->vp_y) / v->vp_h * g_blur_h - .5f;
  if (u < -1 || t < -1 || u > g_blur_w || t > g_blur_h) return 0xFF000000u;
  if (u < 0) u = 0; if (t < 0) t = 0;
  if (u > g_blur_w - 1) u = (float)g_blur_w - 1;
  if (t > g_blur_h - 1) t = (float)g_blur_h - 1;
  int x0 = (int)u, y0 = (int)t;
  int x1 = x0 + 1 < g_blur_w ? x0 + 1 : x0, y1 = y0 + 1 < g_blur_h ? y0 + 1 : y0;
  float fx = u - x0, fy = t - y0;
  uint32_t a = rp_mix(g_blur[y0 * g_blur_w + x0], g_blur[y0 * g_blur_w + x1], fx);
  uint32_t b = rp_mix(g_blur[y1 * g_blur_w + x0], g_blur[y1 * g_blur_w + x1], fx);
  return rp_mix(a, b, fy);
}

/* The frosted panel body: the blurred game, darkened and tinted. */
static void draw_glass(RpCanvas *c, const SnesModalLayerView *v, float rad) {
  float hw = c->w / 2.f, hh = c->h / 2.f;
  for (int y = 0; y < c->h; y++) {
    uint32_t rowcol = 0;
    int last_bx = -99;
    for (int x = 0; x < c->w; x++) {
      float fx = x + .5f, fy = y + .5f;
      float qx = fabsf(fx - hw) - (hw - rad), qy = fabsf(fy - hh) - (hh - rad);
      float d;
      if (qx <= 0 || qy <= 0) d = (qx > qy ? qx : qy) - rad;
      else d = sqrtf(qx * qx + qy * qy) - rad;
      float cov = .5f - d;
      if (cov <= 0) { c->px[(size_t)y * c->w + x] = 0; continue; }
      if (cov > 1) cov = 1;
      int bx = x / 6;
      if (bx != last_bx) {
        uint32_t g = blur_at(v, (float)(g_lx + x), (float)(g_ly + y));
        rowcol = rp_mix(g, C_TINT, 0.74f);
        last_bx = bx;
      }
      float a = 0.93f * cov;
      uint32_t A = (uint32_t)(a * 255.f + .5f);
      uint32_t r = (uint32_t)(((rowcol >> 16) & 255) * a), gg = (uint32_t)(((rowcol >> 8) & 255) * a),
               b = (uint32_t)((rowcol & 255) * a);
      c->px[(size_t)y * c->w + x] = (A << 24) | (r << 16) | (gg << 8) | b;
    }
  }
  rp_stroke_rrect(c, .5f, .5f, c->w - 1.f, c->h - 1.f, rad, 1.2f, 0x30FFFFFFu);
}

/* Controller glyph style: 0 Xbox, 1 PlayStation, 2 Nintendo. */
static int pad_style(void) {
#if SNESRECOMP_SDL3
  int n = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&n);
  int st = 0;
  if (ids && n > 0) {
    SDL_GamepadType t = SDL_GetGamepadTypeForID(ids[0]);
    if (t == SDL_GAMEPAD_TYPE_PS3 || t == SDL_GAMEPAD_TYPE_PS4 || t == SDL_GAMEPAD_TYPE_PS5) st = 1;
    else if (t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO ||
             t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
             t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT ||
             t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR) st = 2;
  }
  SDL_free(ids);
  return st;
#else
  return 0;
#endif
}

enum { G_SOUTH, G_EAST, G_WEST, G_LB, G_RB };

/* One face/shoulder button glyph; returns its width. */
static float draw_glyph(RpCanvas *c, int style, int which, float x, float cy, float s) {
  float r = 13 * s;
  if (which == G_LB || which == G_RB) {
    const char *t = style == 1 ? (which == G_LB ? "L1" : "R1")
                  : style == 2 ? (which == G_LB ? "L" : "R") : (which == G_LB ? "LB" : "RB");
    float w = rp_text_width(RP_BOLD, 14 * s, t) + 16 * s;
    rp_fill_rrect(c, x, cy - r * .85f, w, r * 1.7f, 7 * s, 0xFF46362Fu);
    rp_text_center(c, RP_BOLD, 14 * s, x, cy - r * .85f, w, r * 1.7f, t, C_TEXT);
    return w;
  }
  float cx = x + r;
  if (style == 1) {
    rp_fill_circle(c, cx, cy, r, 0xFF352621u);
    float k = r * .45f;
    if (which == G_SOUTH) {
      rp_line(c, cx - k, cy - k, cx + k, cy + k, 2.4f * s, 0xFF7FA8FFu);
      rp_line(c, cx - k, cy + k, cx + k, cy - k, 2.4f * s, 0xFF7FA8FFu);
    } else if (which == G_EAST) {
      rp_stroke_circle(c, cx, cy, k * 1.1f, 2.4f * s, 0xFFFF6F7Fu);
    } else {
      rp_stroke_rrect(c, cx - k, cy - k, 2 * k, 2 * k, 1.5f * s, 2.4f * s, 0xFFF59BD8u);
    }
    return 2 * r;
  }
  const char *t;
  uint32_t col;
  if (style == 2) {   /* Nintendo labels by position */
    t = which == G_SOUTH ? "B" : which == G_EAST ? "A" : "Y";
    col = 0xFF665650u;
  } else {
    t = which == G_SOUTH ? "A" : which == G_EAST ? "B" : "X";
    col = which == G_SOUTH ? 0xFF3FA34Du : which == G_EAST ? 0xFFD0433Fu : 0xFF3B76D6u;
  }
  rp_fill_circle(c, cx, cy, r, col);
  rp_text_center(c, RP_BOLD, 15 * s, cx - r, cy - r, 2 * r, 2 * r, t, 0xFFFFFFFFu);
  return 2 * r;
}

static float draw_keycap(RpCanvas *c, const char *t, float x, float cy, float s) {
  float h = 24 * s;
  float w = rp_text_width(RP_BOLD, 13 * s, t) + 14 * s;
  if (w < h) w = h;
  rp_fill_rrect(c, x, cy - h / 2, w, h, 6 * s, 0xFF46362Fu);
  rp_stroke_rrect(c, x, cy - h / 2, w, h, 6 * s, 1.f, 0x40FFFFFFu);
  rp_text_center(c, RP_BOLD, 13 * s, x, cy - h / 2, w, h, t, C_TEXT);
  return w;
}

static void draw_hints(RpCanvas *c, float x, float y, float w, float h, float s) {
  rp_fill_rrect(c, x, y, w, h, 12 * s, 0x50000000u);
  float cy = y + h / 2, gap = 8 * s, sep = 18 * s, fs = 16 * s;
  struct { int g; const char *k; const char *k2; const char *label; } it[4] = {
    {G_SOUTH, "Enter", NULL, "Select"},
    {G_EAST, "Esc", NULL, "Back"},
    {G_WEST, "F5", NULL, "Apply"},
    {G_LB, "Q", "E", "Tab"},
  };
  int style = pad_style();
  /* Measure, then scale down to fit. */
  float total = 0;
  float widths[4];
  for (int pass = 0; pass < 2; pass++) {
    RpCanvas dummy = *c;
    dummy.cx0 = dummy.cy0 = 0; dummy.cx1 = dummy.cy1 = 0;  /* draw nothing */
    RpCanvas *dc = pass == 0 ? &dummy : c;
    float px = pass == 0 ? 0 : x + (w - total) / 2;
    for (int i = 0; i < 4; i++) {
      float start = px;
      if (g_last_pad) {
        px += draw_glyph(dc, style, it[i].g, px, cy, s);
        if (it[i].g == G_LB) px += 4 * s + draw_glyph(dc, style, G_RB, px + 4 * s, cy, s);
      } else {
        px += draw_keycap(dc, it[i].k, px, cy, s);
        if (it[i].k2) px += 4 * s + draw_keycap(dc, it[i].k2, px + 4 * s, cy, s);
      }
      px += gap;
      px += rp_text(dc, RP_REGULAR, fs, px, cy - fs / 2, it[i].label, C_DIM, 0);
      widths[i] = px - start;
      if (i < 3) px += sep;
    }
    if (pass == 0) total = px;
  }
  (void)widths;
}

static void wrap_text(RpCanvas *c, int font, float size, float x, float y, float w,
                      int max_lines, const char *s, uint32_t col) {
  char line[256];
  const char *p = s;
  for (int ln = 0; ln < max_lines && *p; ln++) {
    const char *best = NULL, *q = p;
    while (1) {
      const char *sp = strchr(q, ' ');
      const char *end = sp ? sp : q + strlen(q);
      size_t len = (size_t)(end - p);
      if (len >= sizeof(line)) len = sizeof(line) - 1;
      memcpy(line, p, len);
      line[len] = 0;
      if (rp_text_width(font, size, line) > w && best) break;
      best = end;
      if (!sp) break;
      q = sp + 1;
    }
    size_t len = (size_t)(best - p);
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = 0;
    bool last = ln == max_lines - 1 && *best;
    rp_text(c, font, size, x, y + ln * size * 1.3f, last ? p : line, col, w);
    p = *best ? best + 1 : best;
  }
}

static float approach(float cur, float target, float k) {
  if (cur < 0) return target;
  float d = target - cur;
  if (fabsf(d) < .5f) return target;
  return cur + d * k;
}

static bool g_animating;

static void draw_toggle(RpCanvas *c, float x, float cy, float s, int on, bool changed) {
  float w = 50 * s, h = 28 * s;
  rp_fill_rrect(c, x, cy - h / 2, w, h, h / 2, on ? C_ACCENT : 0x40FFFFFFu);
  if (changed) rp_stroke_rrect(c, x - 2 * s, cy - h / 2 - 2 * s, w + 4 * s, h + 4 * s, h / 2 + 2 * s, 1.5f * s, C_ACCENT2);
  float kr = h / 2 - 3.5f * s;
  rp_fill_circle(c, on ? x + w - h / 2 : x + h / 2, cy, kr, 0xFFFFFFFFu);
}

static void draw_row(RpCanvas *c, Row *r, float x, float y, float w, float h, float s,
                     bool focus) {
  float fs = 21 * s;
  float ty = y + (h - fs) / 2;
  float cy = y + h / 2;
  bool changed = r->type != R_ACTION && r->pend != r->cur;
  uint32_t lc = focus ? C_TEXT : 0xFFEFE2D8u;
  float right = x + w;
  float vw = 0;   /* width taken by the value on the right */
  char buf[64];
  if (r->type == R_TOGGLE) {
    vw = 50 * s;
    draw_toggle(c, right - vw, cy, s, r->pend, changed);
  } else if (r->type == R_SLIDER) {
    float tw = w * .30f;
    if (tw > 180 * s) tw = 180 * s;
    snprintf(buf, sizeof(buf), "%d%s", r->pend, r->unit ? r->unit : "");
    float nw = 52 * s;
    float tx = right - tw;
    float frac = r->max > r->min ? (float)(r->pend - r->min) / (r->max - r->min) : 0;
    rp_fill_rrect(c, tx, cy - 3 * s, tw, 6 * s, 3 * s, 0x40FFFFFFu);
    rp_fill_rrect(c, tx, cy - 3 * s, tw * frac > 6 * s ? tw * frac : 6 * s, 6 * s, 3 * s, C_ACCENT);
    rp_fill_circle(c, tx + tw * frac, cy, 9 * s, focus ? 0xFFFFFFFFu : 0xFFF6E6DAu);
    float numw = rp_text_width(RP_BOLD, fs * .9f, buf);
    rp_text(c, RP_BOLD, fs * .9f, tx - 14 * s - numw, y + (h - fs * .9f) / 2, buf,
            changed ? C_ACCENT2 : C_DIM, 0);
    vw = tw + 14 * s + (numw > nw ? numw : nw);
  } else if (r->type == R_PICK || r->type == R_SHADER) {
    const char *v;
    if (r->type == R_SHADER) v = r->pend >= 0 && (size_t)r->pend < g_count ? g_labels[r->pend] : "Off";
    else v = r->choices[pick_index(r, r->pend)];
    float maxv = w * .52f;
    float aw = 16 * s;
    float tw = rp_text_width(RP_BOLD, fs * .95f, v);
    if (tw > maxv) tw = maxv;
    uint32_t vc = changed ? 0xFFFFD27Au : (focus ? C_TEXT : C_DIM);
    if (r->type == R_SHADER) {
      rp_text(c, RP_BOLD, fs * .95f, right - aw - tw - 6 * s, y + (h - fs * .95f) / 2, v, vc, maxv);
      float ax = right - aw * .6f;
      rp_line(c, ax - 5 * s, cy - 7 * s, ax + 2 * s, cy, 2.2f * s, focus ? C_ACCENT : C_DIM);
      rp_line(c, ax + 2 * s, cy, ax - 5 * s, cy + 7 * s, 2.2f * s, focus ? C_ACCENT : C_DIM);
      vw = aw + tw + 6 * s;
    } else {
      uint32_t ac = focus ? C_ACCENT : 0x50FFFFFFu;
      float ax = right - aw * .5f;
      rp_line(c, ax - 3 * s, cy - 6 * s, ax + 3 * s, cy, 2.2f * s, ac);
      rp_line(c, ax + 3 * s, cy, ax - 3 * s, cy + 6 * s, 2.2f * s, ac);
      float vx = right - aw - 6 * s - tw;
      rp_text(c, RP_BOLD, fs * .95f, vx, y + (h - fs * .95f) / 2, v, vc, maxv);
      float lx = vx - 6 * s - aw * .5f;
      rp_line(c, lx + 3 * s, cy - 6 * s, lx - 3 * s, cy, 2.2f * s, ac);
      rp_line(c, lx - 3 * s, cy, lx + 3 * s, cy + 6 * s, 2.2f * s, ac);
      vw = aw * 2 + tw + 12 * s;
    }
  } else if (r->type == R_ACTION) {
    float ax = right - 10 * s;
    if (g_confirm_row == (int)(r - g_rows) && now_s() - g_confirm_at < 3.0) {
      const char *t = "Press again";
      float tw = rp_text_width(RP_BOLD, fs * .85f, t);
      rp_text(c, RP_BOLD, fs * .85f, right - tw, y + (h - fs * .85f) / 2, t, C_ACCENT2, 0);
      vw = tw;
    } else {
      rp_line(c, ax - 5 * s, cy - 7 * s, ax + 2 * s, cy, 2.2f * s, focus ? C_ACCENT : 0x50FFFFFFu);
      rp_line(c, ax + 2 * s, cy, ax - 5 * s, cy + 7 * s, 2.2f * s, focus ? C_ACCENT : 0x50FFFFFFu);
      vw = 16 * s;
    }
  }
  float lw = w - vw - 14 * s;
  rp_text(c, focus ? RP_BOLD : RP_REGULAR, fs, x, ty, r->label, lc, lw > 20 * s ? lw : 20 * s);
}

static void render_panel(const SnesModalLayerView *v) {
  /* Scale with the window height (readable on a TV), and keep the panel to
   * about half the width on narrow windows. */
  float s = v->out_h / 920.f;
  if (640 * s > v->out_w * .56f) s = v->out_w * .56f / 640.f;
  if (s < .42f) s = .42f;
  float margin = 24 * s;
  int pw = (int)(640 * s), ph = (int)(v->out_h - 2 * margin);
  if (pw > v->out_w - 2 * (int)margin) pw = v->out_w - 2 * (int)margin;
  if (pw < 60) pw = 60;
  if (ph < 60) ph = 60;
  size_t need = (size_t)pw * ph;
  if (need > g_layer_cap) {
    free(g_layer);
    g_layer = (uint32_t *)malloc(need * 4);
    g_layer_cap = g_layer ? need : 0;
  }
  if (!g_layer) return;
  g_lw = pw; g_lh = ph;
  g_lx = v->out_w - (int)margin - pw;
  g_ly = (int)margin;
  RpCanvas cv = {g_layer, pw, ph, 0, 0, pw, ph};
  RpCanvas *c = &cv;
  float rad = 26 * s;
  draw_glass(c, v, rad);
  g_animating = false;

  float pad = 28 * s;
  float x = pad, w = pw - 2 * pad;
  float y = pad - 4 * s;
  /* Header */
  rp_text(c, RP_BOLD, 40 * s, x, y, "Options", C_TEXT, w * .6f);
  {
    const char *game = "SUPER RYU WORLD 1";
    float gs = 14 * s;
    float gw = rp_text_width(RP_BOLD, gs, game);
    rp_text(c, RP_BOLD, gs, x + w - gw, y + 16 * s, game, C_ACCENT2, w * .38f);
  }
  y += 50 * s;
  {
    bool live = OpenGLRenderer_IsActive();
    rp_fill_circle(c, x + 5 * s, y + 9 * s, 4.5f * s, live ? 0xFF5BE08Au : 0xFFE0B45Bu);
    char line[200];
    snprintf(line, sizeof(line), "%s", live ? "Live preview  \xC2\xB7  OpenGL"
                                            : "SDL renderer  \xC2\xB7  shaders need OpenGL");
    rp_text(c, RP_REGULAR, 17 * s, x + 16 * s, y, line, C_DIM, w - 16 * s);
  }
  y += 34 * s;
  /* Tabs */
  float th = 44 * s;
  rp_fill_rrect(c, x, y, w, th, 12 * s, 0x38000000u);
  float tabw = w / T_COUNT;
  float tfs = 17 * s;
  for (int i = 0; i < T_COUNT; i++) {
    float need_w = rp_text_width(RP_BOLD, tfs, kTabNames[i]);
    if (need_w > tabw - 8 * s) tfs = tfs * (tabw - 8 * s) / need_w;
  }
  float target = x + tabw * g_tab;
  g_tab_x = approach(g_tab_x, target, .38f);
  if (g_tab_x != target) g_animating = true;
  rp_fill_rrect(c, g_tab_x + 3 * s, y + 3 * s, tabw - 6 * s, th - 6 * s, 10 * s,
                g_list ? 0x60FFFFFFu & 0x40FFFFFF : rp_alpha(C_ACCENT, .85f));
  for (int i = 0; i < T_COUNT; i++)
    rp_text_center(c, i == g_tab ? RP_BOLD : RP_REGULAR, tfs, x + tabw * i, y, tabw, th,
                   kTabNames[i], i == g_tab ? 0xFFFFFFFFu : C_DIM);
  y += th + 18 * s;

  /* Footer layout from the bottom up. */
  float hint_h = 46 * s;
  float hint_y = ph - pad - hint_h + 6 * s;
  float ab_h = 56 * s;
  float ab_y = hint_y - 14 * s - ab_h;
  float desc_fs = 16 * s;
  float desc_h = desc_fs * 1.3f * 2 + desc_fs * 1.3f;   /* 2 lines + status */
  float desc_y = ab_y - 12 * s - desc_h;
  float rows_top = y, rows_bot = desc_y - 8 * s;
  float rh = 56 * s;

  if (g_list) {
    /* Shader list */
    int vis = (int)((rows_bot - rows_top) / (rh * .78f));
    if (vis < 1) vis = 1;
    float lh = (rows_bot - rows_top) / vis;
    if (g_list_top < 0) g_list_top = g_list_sel - vis / 2;
    if (g_list_sel < g_list_top) g_list_top = g_list_sel;
    if (g_list_sel >= g_list_top + vis) g_list_top = g_list_sel - vis + 1;
    if (g_list_top > (int)g_count - vis) g_list_top = (int)g_count - vis;
    if (g_list_top < 0) g_list_top = 0;
    int si = shader_row_index();
    rp_canvas_clip(c, 0, (int)rows_top, pw, (int)rows_bot);
    for (int i = 0; i < vis && g_list_top + i < (int)g_count; i++) {
      int k = g_list_top + i;
      float ry = rows_top + i * lh;
      bool f = k == g_list_sel;
      if (f) rp_fill_rrect(c, x - 10 * s, ry + 2 * s, w + 20 * s, lh - 4 * s, 12 * s, rp_alpha(C_ACCENT, .30f));
      bool chosen = si >= 0 && g_rows[si].pend == k;
      bool live = k == g_preview;
      float fsz = 19 * s;
      const char *lab = g_labels[k];
      rp_text(c, f ? RP_BOLD : RP_REGULAR, fsz, x + 4 * s, ry + (lh - fsz) / 2, lab,
              f ? C_TEXT : (shader_live_ok(k) ? 0xFFEEDDD0u : 0xFF968676u), w - 80 * s);
      if (chosen) rp_fill_circle(c, x + w - 10 * s, ry + lh / 2, 5 * s, C_ACCENT2);
      if (live && !chosen) rp_stroke_circle(c, x + w - 10 * s, ry + lh / 2, 5 * s, 1.5f * s, C_ACCENT);
    }
    rp_canvas_noclip(c);
    /* scrollbar */
    if ((int)g_count > vis) {
      float sh = (rows_bot - rows_top);
      float bh = sh * vis / g_count;
      if (bh < 20 * s) bh = 20 * s;
      float by = rows_top + (sh - bh) * g_list_top / (float)((int)g_count - vis);
      rp_fill_rrect(c, x + w + 12 * s, by, 4 * s, bh, 2 * s, 0x60FFFFFFu);
    }
    char d[200];
    snprintf(d, sizeof(d), "%d of %zu presets. Highlight to preview, A/Enter picks, "
             "Left/Right pages.", g_list_sel, g_count - 1);
    wrap_text(c, RP_REGULAR, desc_fs, x, desc_y, w, 2, d, C_DIM);
  } else {
    int idx[DC_MAX_ROWS];
    int n = tab_rows(g_tab, idx);
    if (g_focus > n) g_focus = n;
    /* Rows (+ a note) scroll to keep the focus in view. */
    float avail = rows_bot - rows_top;
    int vis = (int)(avail / rh);
    if (vis < 1) vis = 1;
    static int top;
    int f = g_focus < n ? g_focus : n - 1;
    if (f < top) top = f;
    if (f >= top + vis) top = f - vis + 1;
    if (top > n - vis) top = n - vis;
    if (top < 0) top = 0;
    rp_canvas_clip(c, 0, (int)rows_top, pw, (int)rows_bot);
    if (g_focus < n) {
      float target_y = rows_top + (g_focus - top) * rh;
      g_hl_y = approach(g_hl_y, target_y, .40f);
      if (g_hl_y != target_y) g_animating = true;
      rp_fill_rrect(c, x - 12 * s, g_hl_y + 3 * s, w + 24 * s, rh - 6 * s, 14 * s, rp_alpha(C_ACCENT, .26f));
      rp_fill_rrect(c, x - 12 * s, g_hl_y + 14 * s, 4 * s, rh - 28 * s, 2 * s, C_ACCENT2);
    } else {
      g_hl_y = -1;
    }
    float ry = rows_top;
    for (int i = top; i < n && i < top + vis; i++, ry += rh) {
      Row *r = &g_rows[idx[i]];
      draw_row(c, r, x + 4 * s, ry, w - 8 * s, rh, s, i == g_focus);
      if (i + 1 < n && i + 1 < top + vis && i + 1 != g_focus && i != g_focus)
        rp_fill_rrect(c, x, ry + rh - .5f * s, w, 1.f, 0, 0x14FFFFFFu);
    }
    /* Tab notes below the rows */
    for (int i = 0; i < g_nrows; i++)
      if (g_rows[i].tab == g_tab && g_rows[i].type == R_NOTE && ry + rh <= rows_bot + 1) {
        rp_fill_rrect(c, x, ry + 8 * s, w, rh - 16 * s, 10 * s, rp_alpha(C_ACCENT2, .16f));
        rp_text(c, RP_REGULAR, 16 * s, x + 12 * s, ry + (rh - 16 * s) / 2, g_rows[i].label, 0xFFFFE2B8u, w - 24 * s);
        ry += rh;
      }
    rp_canvas_noclip(c);
    if (n > vis) {
      float sh = avail;
      float bh = sh * vis / n;
      float by = rows_top + (sh - bh) * top / (float)(n - vis);
      rp_fill_rrect(c, x + w + 12 * s, by, 4 * s, bh, 2 * s, 0x60FFFFFFu);
    }
    Row *fr = focused_row();
    const char *d = fr && fr->desc ? fr->desc
                  : pending_count() ? "Apply keeps your changes. Back discards them."
                                    : "Everything is applied.";
    wrap_text(c, RP_REGULAR, desc_fs, x, desc_y, w, 2, d, C_DIM);
  }
  /* Status line */
  {
    char st[300];
    int pc = pending_count();
    if (g_status[0] && now_s() - g_status_at < 6.0) snprintf(st, sizeof(st), "%s", g_status);
    else if (pc) snprintf(st, sizeof(st), "%d change%s not applied", pc, pc == 1 ? "" : "s");
    else st[0] = 0;
    if (st[0]) rp_text(c, RP_BOLD, desc_fs, x, desc_y + desc_fs * 1.3f * 2, st,
                       pc && !(g_status[0] && now_s() - g_status_at < 6.0) ? 0xFFFFD27Au : 0xFFFFE2B8u, w);
  }
  /* Apply */
  {
    int idxn = tab_rows(g_tab, NULL);
    bool f = !g_list && g_focus == idxn;
    int pc = pending_count();
    uint32_t col = pc ? C_ACCENT : rp_alpha(C_ACCENT, .45f);
    if (f) col = rp_mix(col, 0xFFFFFFFFu, .12f);
    rp_fill_rrect(c, x, ab_y, w, ab_h, 14 * s, col);
    if (f) rp_stroke_rrect(c, x - 3 * s, ab_y - 3 * s, w + 6 * s, ab_h + 6 * s, 16 * s, 2 * s, 0xE0FFFFFFu);
    char t[32];
    if (pc) snprintf(t, sizeof(t), "Apply (%d)", pc);
    else snprintf(t, sizeof(t), "Apply");
    rp_text_center(c, RP_BOLD, 22 * s, x, ab_y, w, ab_h, t, 0xFFFFFFFFu);
  }
  draw_hints(c, x, hint_y, w, hint_h, s);
  rp_finish(c);
}

/* ---- host entry points --------------------------------------------------- */

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
  g_preview = g_current;
  load_motion();
  fprintf(stderr, "[srw1] %zu shader presets under %s; startup=%s\n", n,
          SRW1_PRESET_ROOT, g_current ? g_labels[g_current] : "Off");
  build_rows();
}

const char *Srw1Options_StartupShader(void) {
  Srw1Options_Init();
  return g_current > 0 ? g_paths[g_current] : NULL;
}

bool Srw1Options_IsOpen(void) { return g_open; }

void Srw1Options_Toggle(void) {
  Srw1Options_Init();
  if (g_open) close_menu(true); else open_menu();
}

static bool feed(MenuIn in, bool pressed) {
  if (pressed) menu_input(in);
  return true;
}

bool Srw1Options_HandleKey(int key, bool pressed, bool repeat) {
  Srw1Options_Init();
  int mod = (int)SDL_GetModState();
  /* F12 and Escape always belong to the panel (Escape never quits), exactly
   * as in v1.0.x; every other key reaches the game while it is closed. */
  if (key == SDLK_F12) {
    if (pressed && !repeat) { g_last_pad = 0; Srw1Options_Toggle(); }
    return true;
  }
  if (key == SDLK_ESCAPE) {
    if (pressed && !repeat) {
      g_last_pad = 0;
      if (!g_open) open_menu();
      else menu_input(IN_BACK);
      g_dirty = 1;
    }
    return true;
  }
  if (!g_open) return false;   /* gameplay keys untouched while closed */
  if (pressed) { g_last_pad = 0; g_dirty = 1; }
  switch (key) {
  case SDLK_UP: return feed(IN_UP, pressed);
  case SDLK_DOWN: return feed(IN_DOWN, pressed);
  case SDLK_LEFT: return feed(IN_LEFT, pressed);
  case SDLK_RIGHT: return feed(IN_RIGHT, pressed);
  case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: return feed(IN_OK, pressed);
  case SDLK_BACKSPACE: return feed(IN_BACK, pressed);
  case SDLK_F5: return feed(IN_APPLY, pressed);
  case 'q': return feed(IN_TAB_PREV, pressed);
  case 'e': return feed(IN_TAB_NEXT, pressed);
  case SDLK_TAB: return feed((mod & 0x0003) ? IN_TAB_PREV : IN_TAB_NEXT, pressed);
  case SDLK_PAGEUP: return feed(IN_PAGE_UP, pressed);
  case SDLK_PAGEDOWN: return feed(IN_PAGE_DOWN, pressed);
  default: return true; /* swallow everything else while open */
  }
}

static int g_held = -1;
static double g_held_next;

bool Srw1Options_HandleButton(int b, bool pressed) {
  Srw1Options_Init();
  if (b == SDL_CONTROLLER_BUTTON_GUIDE) {
    if (pressed) { g_last_pad = 1; Srw1Options_Toggle(); }
    return true;
  }
  /* Closed: every button goes to the game exactly as before. */
  if (!g_open) return false;
  if (pressed) { g_last_pad = 1; g_dirty = 1; }
  MenuIn in;
  switch (b) {
  case SDL_CONTROLLER_BUTTON_DPAD_UP: in = IN_UP; break;
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN: in = IN_DOWN; break;
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: in = IN_LEFT; break;
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: in = IN_RIGHT; break;
  case SDL_CONTROLLER_BUTTON_A: in = IN_OK; break;
  case SDL_CONTROLLER_BUTTON_B: in = IN_BACK; break;
  case SDL_CONTROLLER_BUTTON_X: in = IN_APPLY; break;
  case SDL_CONTROLLER_BUTTON_START: in = IN_APPLY; break;
  case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: in = IN_TAB_PREV; break;
  case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: in = IN_TAB_NEXT; break;
  default: return true;   /* swallowed while open */
  }
  if (in <= IN_RIGHT) {
    if (pressed) { g_held = (int)in; g_held_next = now_s() + 0.38; }
    else if (g_held == (int)in) g_held = -1;
  }
  return feed(in, pressed);
}

/* Test hooks (no input device): SRW1_OPTIONS_OPEN_AT=<ms after start> opens
 * the panel; SRW1_OPTIONS_KEYS=<list> then feeds one input per 160 ms:
 * u d l r o(k) b(ack) a(pply) [ ] (tabs) P/K (pad/keyboard hints). */
void Srw1Options_Tick(void) {
  Srw1Options_Init();
  static long open_at = -2;
  static const char *keys;
  static double next_key;
  if (open_at == -2) {
    const char *e = getenv("SRW1_OPTIONS_OPEN_AT");
    open_at = e && *e ? atol(e) : -1;
    keys = getenv("SRW1_OPTIONS_KEYS");
  }
  double t = now_s();
  if (open_at >= 0 && SDL_GetTicks() >= (uint64_t)open_at) {
    open_at = -1;
    open_menu();
    next_key = t + 0.6;
  }
  if (!g_open) { g_held = -1; return; }
  if (g_held >= 0 && t >= g_held_next) {
    menu_input((MenuIn)g_held);
    g_held_next = t + 0.085;
  }
  if (g_list && g_list_moved_at > 0 && t - g_list_moved_at > 0.35) {
    g_list_moved_at = 0;
    preview_shader(g_list_sel);
  }
  if (keys && *keys && t >= next_key) {
    next_key = t + 0.16;
    switch (*keys++) {
    case 'u': menu_input(IN_UP); break;
    case 'd': menu_input(IN_DOWN); break;
    case 'l': menu_input(IN_LEFT); break;
    case 'r': menu_input(IN_RIGHT); break;
    case 'o': menu_input(IN_OK); break;
    case 'b': menu_input(IN_BACK); break;
    case 'a': menu_input(IN_APPLY); break;
    case '[': menu_input(IN_TAB_PREV); break;
    case ']': menu_input(IN_TAB_NEXT); break;
    case 'P': g_last_pad = 1; g_dirty = 1; break;
    case 'K': g_last_pad = 0; g_dirty = 1; break;
    case 'w': next_key = t + 1.5; break;
    default: break;
    }
  }
}

static int g_frame_w, g_frame_h;

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
  g_frame_w = width;
  g_frame_h = height;
}

void Srw1Options_OnPresent(uint8_t *pixels, int pitch, int width, int height) {
  if (!g_open) keep_copy(pixels, pitch, width, height);
}

void Srw1Options_RedrawPaused(uint8_t *pixels, int pitch, int width, int height) {
  if (g_frame_copy && g_frame_w == width && g_frame_h == height)
    for (int y = 0; y < height; y++)
      memcpy(pixels + (size_t)y * pitch, g_frame_copy + (size_t)y * width * 4, (size_t)width * 4);
}

bool Srw1Options_Layer(int out_w, int out_h, int vp_x, int vp_y, int vp_w, int vp_h,
                       const uint32_t **argb, int *x, int *y, int *w, int *h) {
  if (!g_open || out_w <= 0 || out_h <= 0) return false;
  SnesModalLayerView v;
  v.out_w = out_w; v.out_h = out_h;
  v.vp_x = vp_x; v.vp_y = vp_y; v.vp_w = vp_w; v.vp_h = vp_h;
  v.frame = (const uint32_t *)g_frame_copy;
  v.frame_w = g_frame_w; v.frame_h = g_frame_h;
  static double last_open;
  if (g_opened_at != last_open) {
    make_blur(&v);
    last_open = g_opened_at;
    g_dirty = 1;
  }
  if (out_w != g_last_out_w || out_h != g_last_out_h) {
    g_last_out_w = out_w;
    g_last_out_h = out_h;
    g_dirty = 1;
  }
  static double last_tick;
  double t = now_s();
  if (t - last_tick > 0.25) { last_tick = t; g_dirty = 1; }
  if (g_dirty || g_animating || !g_layer) {
    g_dirty = 0;
    render_panel(&v);
  }
  if (!g_layer) return false;
  *argb = g_layer;
  *x = g_lx; *y = g_ly; *w = g_lw; *h = g_lh;
  return true;
}
