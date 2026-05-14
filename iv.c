#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <strings.h>

typedef struct {
  char **items;
  int count;
} ImageList;

typedef struct {
  SDL_Window *window;
  SDL_Surface *image;
  TTF_Font *ui_font;
  ImageList list;
  int current_index;
  int rotation;
  float zoom;
  int fullscreen;
} Viewer;

typedef struct {
  char *display_name;
  char *path;
  int is_dir;
} BrowserEntry;

typedef struct {
  BrowserEntry *entries;
  int count;
  int capacity;
  char *current_dir;
  int selected_index;
  int scroll_offset;
} BrowserState;

static char *dup_string(const char *src) {
  size_t len = strlen(src);
  char *copy = malloc(len + 1);
  if (!copy) return NULL;
  memcpy(copy, src, len + 1);
  return copy;
}

static char *path_dirname(const char *path) {
  size_t len = strlen(path);
  while (len > 1 && path[len - 1] == '/') {
    len--;
  }

  const char *slash = NULL;
  for (size_t i = 0; i < len; i++) {
    if (path[i] == '/') slash = path + i;
  }

  if (!slash) {
    return dup_string(".");
  }

  if (slash == path) {
    return dup_string("/");
  }

  size_t dir_len = (size_t)(slash - path);
  char *dir = malloc(dir_len + 1);
  if (!dir) return NULL;
  memcpy(dir, path, dir_len);
  dir[dir_len] = '\0';
  return dir;
}

static const char *path_basename(const char *path) {
  size_t len = strlen(path);
  while (len > 1 && path[len - 1] == '/') {
    len--;
  }

  const char *base = path;
  for (size_t i = 0; i < len; i++) {
    if (path[i] == '/') base = path + i + 1;
  }
  return base;
}

static char *join_path(const char *dir, const char *name) {
  size_t dir_len = strlen(dir);
  size_t name_len = strlen(name);
  int needs_sep = dir_len > 0 && strcmp(dir, "/") != 0;
  char *path = malloc(dir_len + name_len + (needs_sep ? 2 : 1));
  if (!path) return NULL;

  if (needs_sep) {
    snprintf(path, dir_len + name_len + 2, "%s/%s", dir, name);
  } else {
    snprintf(path, dir_len + name_len + 1, "%s%s", dir, name);
  }

  return path;
}

static int file_exists_regular(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int is_directory(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_supported_image_name(const char *name);

static void free_browser_state(BrowserState *state) {
  if (!state) return;
  for (int i = 0; i < state->count; i++) {
    free(state->entries[i].display_name);
    free(state->entries[i].path);
  }
  free(state->entries);
  free(state->current_dir);
  memset(state, 0, sizeof(*state));
}

static int browser_entry_cmp(const void *a, const void *b) {
  const BrowserEntry *ea = a;
  const BrowserEntry *eb = b;
  if (strcmp(ea->display_name, "..") == 0) return -1;
  if (strcmp(eb->display_name, "..") == 0) return 1;
  if (ea->is_dir != eb->is_dir) {
    return eb->is_dir - ea->is_dir;
  }
  return strcasecmp(ea->display_name, eb->display_name);
}

static int browser_add_entry(BrowserState *state, const char *display_name, const char *path, int is_dir) {
  if (state->count == state->capacity) {
    int next_capacity = state->capacity == 0 ? 32 : state->capacity * 2;
    BrowserEntry *next = realloc(state->entries, (size_t)next_capacity * sizeof(BrowserEntry));
    if (!next) return 0;
    state->entries = next;
    state->capacity = next_capacity;
  }

  BrowserEntry *entry = &state->entries[state->count++];
  memset(entry, 0, sizeof(*entry));
  entry->display_name = dup_string(display_name);
  entry->path = dup_string(path);
  entry->is_dir = is_dir;
  if (!entry->display_name || !entry->path) {
    free(entry->display_name);
    free(entry->path);
    state->count--;
    return 0;
  }
  return 1;
}

static int browser_set_dir(BrowserState *state, const char *dir) {
  DIR *handle = NULL;
  struct dirent *entry;
  char *normalized = NULL;
  char *parent = NULL;
  int ok = 0;

  if (!state || !dir) return 0;

  handle = opendir(dir);
  if (!handle) return 0;

  normalized = realpath(dir, NULL);
  if (!normalized) {
    normalized = dup_string(dir);
  }
  if (!normalized) goto cleanup;

  free_browser_state(state);
  memset(state, 0, sizeof(*state));
  state->current_dir = normalized;
  normalized = NULL;

  if (strcmp(state->current_dir, "/") != 0) {
    parent = path_dirname(state->current_dir);
    if (!parent) goto cleanup;
    if (!browser_add_entry(state, "..", parent, 1)) goto cleanup;
  }

  while ((entry = readdir(handle)) != NULL) {
    if (entry->d_name[0] == '.' &&
        (entry->d_name[1] == '\0' ||
         (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
      continue;
    }

    if (entry->d_name[0] == '.') {
      continue;
    }

    char *full_path = join_path(state->current_dir, entry->d_name);
    if (!full_path) goto cleanup;

    if (is_directory(full_path)) {
      ok = browser_add_entry(state, entry->d_name, full_path, 1);
      free(full_path);
      if (!ok) goto cleanup;
      ok = 0;
      continue;
    }

    if (file_exists_regular(full_path) && is_supported_image_name(entry->d_name)) {
      ok = browser_add_entry(state, entry->d_name, full_path, 0);
      free(full_path);
      if (!ok) goto cleanup;
      ok = 0;
      continue;
    }

    free(full_path);
  }

  qsort(state->entries, (size_t)state->count, sizeof(BrowserEntry), browser_entry_cmp);
  state->selected_index = 0;
  state->scroll_offset = 0;
  ok = 1;

cleanup:
  free(parent);
  free(normalized);
  if (!ok) {
    if (handle) closedir(handle);
    free_browser_state(state);
    return 0;
  }

  if (handle) closedir(handle);
  return 1;
}

static int browser_visible_rows(int window_h, int row_h, int top_margin, int bottom_margin) {
  int usable = window_h - top_margin - bottom_margin;
  if (usable < row_h) return 1;
  return usable / row_h;
}

static void browser_ensure_visible(BrowserState *state, int visible_rows) {
  if (state->selected_index < state->scroll_offset) {
    state->scroll_offset = state->selected_index;
  } else if (state->selected_index >= state->scroll_offset + visible_rows) {
    state->scroll_offset = state->selected_index - visible_rows + 1;
  }
  if (state->scroll_offset < 0) state->scroll_offset = 0;
}

static void browser_move_selection(BrowserState *state, int delta, int visible_rows) {
  if (state->count <= 0) return;
  state->selected_index += delta;
  if (state->selected_index < 0) state->selected_index = 0;
  if (state->selected_index >= state->count) state->selected_index = state->count - 1;
  browser_ensure_visible(state, visible_rows);
}

static void browser_jump_to(BrowserState *state, int index, int visible_rows) {
  if (state->count <= 0) return;
  if (index < 0) index = 0;
  if (index >= state->count) index = state->count - 1;
  state->selected_index = index;
  browser_ensure_visible(state, visible_rows);
}

static char *browser_activate_selected(BrowserState *state) {
  if (!state || state->selected_index < 0 || state->selected_index >= state->count) return NULL;
  BrowserEntry *entry = &state->entries[state->selected_index];
  if (entry->is_dir) {
    if (!browser_set_dir(state, entry->path)) return NULL;
    return NULL;
  }
  return dup_string(entry->path);
}

static const char *find_ui_font_path(void) {
  static const char *candidates[] = {
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    NULL
  };

  for (int i = 0; candidates[i]; i++) {
    if (access(candidates[i], R_OK) == 0) {
      return candidates[i];
    }
  }
  return NULL;
}

static void render_text(SDL_Surface *surface, TTF_Font *font, int x, int y, SDL_Color color, const char *text) {
  SDL_Surface *label = TTF_RenderUTF8_Blended(font, text, color);
  if (!label) return;
  SDL_Rect dst = {x, y, label->w, label->h};
  SDL_BlitSurface(label, NULL, surface, &dst);
  SDL_FreeSurface(label);
}

typedef struct {
  SDL_Color bg0;
  SDL_Color bg1;
  SDL_Color panel;
  SDL_Color panel_alt;
  SDL_Color panel_soft;
  SDL_Color border;
  SDL_Color border_soft;
  SDL_Color text;
  SDL_Color muted;
  SDL_Color accent;
  SDL_Color accent_soft;
  SDL_Color accent_text;
} SlateTheme;

static SDL_Color slate_color(Uint8 r, Uint8 g, Uint8 b) {
  SDL_Color c = {r, g, b, 255};
  return c;
}

static SlateTheme slate_theme(void) {
  SlateTheme t;
  t.bg0 = slate_color(15, 23, 42);
  t.bg1 = slate_color(30, 41, 59);
  t.panel = slate_color(15, 23, 42);
  t.panel_alt = slate_color(30, 41, 59);
  t.panel_soft = slate_color(51, 65, 85);
  t.border = slate_color(71, 85, 105);
  t.border_soft = slate_color(51, 65, 85);
  t.text = slate_color(248, 250, 252);
  t.muted = slate_color(148, 163, 184);
  t.accent = slate_color(59, 130, 246);
  t.accent_soft = slate_color(37, 99, 235);
  t.accent_text = slate_color(240, 249, 255);
  return t;
}

static void put_pixel(SDL_Surface *surface, int x, int y, Uint32 color);
static void draw_line(SDL_Surface *surface, int x0, int y0, int x1, int y1, Uint32 color);

static void fill_vertical_gradient(SDL_Surface *surface, SDL_Color top, SDL_Color bottom) {
  int w = surface->w;
  int h = surface->h;

  SDL_LockSurface(surface);
  for (int y = 0; y < h; y++) {
    Uint8 r = (Uint8)((top.r * (h - 1 - y) + bottom.r * y) / (h > 1 ? h - 1 : 1));
    Uint8 g = (Uint8)((top.g * (h - 1 - y) + bottom.g * y) / (h > 1 ? h - 1 : 1));
    Uint8 b = (Uint8)((top.b * (h - 1 - y) + bottom.b * y) / (h > 1 ? h - 1 : 1));
    Uint32 px = SDL_MapRGB(surface->format, r, g, b);
    for (int x = 0; x < w; x++) {
      put_pixel(surface, x, y, px);
    }
  }
  SDL_UnlockSurface(surface);
}

static void fill_horizontal_gradient(SDL_Surface *surface, SDL_Rect rect, SDL_Color left, SDL_Color right) {
  SDL_LockSurface(surface);
  for (int x = 0; x < rect.w; x++) {
    Uint8 r = (Uint8)((left.r * (rect.w - 1 - x) + right.r * x) / (rect.w > 1 ? rect.w - 1 : 1));
    Uint8 g = (Uint8)((left.g * (rect.w - 1 - x) + right.g * x) / (rect.w > 1 ? rect.w - 1 : 1));
    Uint8 b = (Uint8)((left.b * (rect.w - 1 - x) + right.b * x) / (rect.w > 1 ? rect.w - 1 : 1));
    Uint32 px = SDL_MapRGB(surface->format, r, g, b);
    for (int y = 0; y < rect.h; y++) {
      put_pixel(surface, rect.x + x, rect.y + y, px);
    }
  }
  SDL_UnlockSurface(surface);
}

static void draw_panel(SDL_Surface *surface, SDL_Rect rect, SDL_Color fill, SDL_Color border) {
  SDL_FillRect(surface, &rect, SDL_MapRGB(surface->format, fill.r, fill.g, fill.b));

  SDL_Rect top = {rect.x, rect.y, rect.w, 1};
  SDL_Rect bottom = {rect.x, rect.y + rect.h - 1, rect.w, 1};
  SDL_Rect left = {rect.x, rect.y, 1, rect.h};
  SDL_Rect right = {rect.x + rect.w - 1, rect.y, 1, rect.h};
  SDL_FillRect(surface, &top, SDL_MapRGB(surface->format, border.r, border.g, border.b));
  SDL_FillRect(surface, &bottom, SDL_MapRGB(surface->format, border.r, border.g, border.b));
  SDL_FillRect(surface, &left, SDL_MapRGB(surface->format, border.r, border.g, border.b));
  SDL_FillRect(surface, &right, SDL_MapRGB(surface->format, border.r, border.g, border.b));
}

static void draw_border(SDL_Surface *surface, SDL_Rect rect, SDL_Color border) {
  SDL_Rect top = {rect.x, rect.y, rect.w, 1};
  SDL_Rect bottom = {rect.x, rect.y + rect.h - 1, rect.w, 1};
  SDL_Rect left = {rect.x, rect.y, 1, rect.h};
  SDL_Rect right = {rect.x + rect.w - 1, rect.y, 1, rect.h};
  SDL_FillRect(surface, &top, SDL_MapRGB(surface->format, border.r, border.g, border.b));
  SDL_FillRect(surface, &bottom, SDL_MapRGB(surface->format, border.r, border.g, border.b));
  SDL_FillRect(surface, &left, SDL_MapRGB(surface->format, border.r, border.g, border.b));
  SDL_FillRect(surface, &right, SDL_MapRGB(surface->format, border.r, border.g, border.b));
}

static void draw_chip(SDL_Surface *surface, SDL_Rect rect, SDL_Color fill, SDL_Color border) {
  SDL_FillRect(surface, &rect, SDL_MapRGB(surface->format, fill.r, fill.g, fill.b));
  SDL_Rect top = {rect.x, rect.y, rect.w, 1};
  SDL_Rect bottom = {rect.x, rect.y + rect.h - 1, rect.w, 1};
  SDL_Rect left = {rect.x, rect.y, 1, rect.h};
  SDL_Rect right = {rect.x + rect.w - 1, rect.y, 1, rect.h};
  SDL_FillRect(surface, &top, SDL_MapRGB(surface->format, border.r, border.g, border.b));
  SDL_FillRect(surface, &bottom, SDL_MapRGB(surface->format, border.r, border.g, border.b));
  SDL_FillRect(surface, &left, SDL_MapRGB(surface->format, border.r, border.g, border.b));
  SDL_FillRect(surface, &right, SDL_MapRGB(surface->format, border.r, border.g, border.b));
}

static void draw_button(SDL_Surface *surface, SDL_Rect rect, SDL_Color fill, SDL_Color border) {
  draw_chip(surface, rect, fill, border);
}

static void draw_chevron(SDL_Surface *surface, SDL_Rect rect, int direction, SDL_Color color) {
  int cy = rect.y + rect.h / 2;
  int left = rect.x + rect.w / 2 - 5;
  int right = rect.x + rect.w / 2 + 5;
  int top = rect.y + rect.h / 2 - 7;
  int bottom = rect.y + rect.h / 2 + 7;

  if (direction < 0) {
    draw_line(surface, right, top, left, cy, SDL_MapRGB(surface->format, color.r, color.g, color.b));
    draw_line(surface, left, cy, right, bottom, SDL_MapRGB(surface->format, color.r, color.g, color.b));
  } else {
    draw_line(surface, left, top, right, cy, SDL_MapRGB(surface->format, color.r, color.g, color.b));
    draw_line(surface, right, cy, left, bottom, SDL_MapRGB(surface->format, color.r, color.g, color.b));
  }
}

static void draw_icon_badge(SDL_Surface *surface, SDL_Rect rect, const char *label, SDL_Color fill, SDL_Color border, SDL_Color text, TTF_Font *font) {
  draw_chip(surface, rect, fill, border);
  render_text(surface, font, rect.x + 10, rect.y + 6, text, label);
}

static void render_browser(SDL_Window *window, TTF_Font *font, BrowserState *state) {
  SlateTheme theme = slate_theme();
  int w = 0;
  int h = 0;
  SDL_GetWindowSize(window, &w, &h);
  SDL_Surface *surface = SDL_GetWindowSurface(window);
  fill_vertical_gradient(surface, theme.bg0, theme.bg1);

  SDL_Rect frame = {18, 18, w - 36, h - 36};
  draw_panel(surface, frame, theme.panel, theme.border);

  SDL_Rect header = {frame.x + 16, frame.y + 16, frame.w - 32, 72};
  fill_horizontal_gradient(surface, header, theme.panel_alt, theme.panel_soft);
  draw_border(surface, header, theme.border);
  render_text(surface, font, header.x + 18, header.y + 13, theme.text, "Open Image");
  render_text(surface, font, header.x + 18, header.y + 39, theme.muted, "Browse folders and pick an image");

  SDL_Rect header_badge = {header.x + header.w - 168, header.y + 18, 148, 30};
  draw_chip(surface, header_badge, theme.panel, theme.border_soft);
  char header_count[64];
  snprintf(header_count, sizeof(header_count), "%d items", state->count);
  render_text(surface, font, header_badge.x + 14, header_badge.y + 6, theme.text, header_count);

  SDL_Rect path_panel = {frame.x + 16, header.y + header.h + 12, frame.w - 32, 52};
  draw_panel(surface, path_panel, theme.panel_alt, theme.border_soft);
  render_text(surface, font, path_panel.x + 16, path_panel.y + 14, theme.muted, "Current");
  render_text(surface, font, path_panel.x + 110, path_panel.y + 14, theme.text, state->current_dir ? state->current_dir : ".");

  SDL_Rect list_panel = {frame.x + 16, path_panel.y + path_panel.h + 12, frame.w - 32,
                         frame.h - (path_panel.y + path_panel.h + 12) - 74};
  draw_panel(surface, list_panel, theme.panel, theme.border_soft);

  int top = list_panel.y + 14;
  int bottom = 20;
  int row_h = TTF_FontHeight(font) + 14;
  int visible = browser_visible_rows(list_panel.h - 28, row_h, 0, bottom);
  browser_ensure_visible(state, visible);

  if (state->count == 0) {
    render_text(surface, font, list_panel.x + 18, list_panel.y + 18, theme.muted, "No image files found in this folder.");
  }

  for (int i = state->scroll_offset; i < state->count && i < state->scroll_offset + visible; i++) {
    int y = top + (i - state->scroll_offset) * row_h;
    SDL_Rect row = {list_panel.x + 12, y, list_panel.w - 24, row_h - 6};
    if (i == state->selected_index) {
      fill_horizontal_gradient(surface, row, theme.accent_soft, theme.accent);
      draw_border(surface, row, theme.accent);
    } else {
      draw_panel(surface, row, theme.panel_alt, theme.border_soft);
    }

    BrowserEntry *entry = &state->entries[i];
    SDL_Rect badge = {row.x + 12, row.y + 8, 72, 24};
    if (entry->is_dir) {
      draw_icon_badge(surface, badge, "DIR", theme.panel, theme.border, theme.text, font);
    } else {
      draw_icon_badge(surface, badge, "IMG", theme.panel_soft, theme.border_soft, theme.text, font);
    }

    render_text(surface, font, row.x + 104, row.y + 7, theme.text, entry->display_name);
    render_text(surface, font, row.x + row.w - 28, row.y + 7, theme.muted, entry->is_dir ? "/" : "");
  }

  SDL_Rect footer = {frame.x + 16, frame.y + frame.h - 52, frame.w - 32, 36};
  draw_panel(surface, footer, theme.panel_alt, theme.border_soft);
  SDL_Rect chip1 = {footer.x + 14, footer.y + 7, 116, 22};
  SDL_Rect chip2 = {chip1.x + chip1.w + 10, footer.y + 7, 114, 22};
  SDL_Rect chip3 = {chip2.x + chip2.w + 10, footer.y + 7, 132, 22};
  draw_chip(surface, chip1, theme.panel, theme.border_soft);
  draw_chip(surface, chip2, theme.panel, theme.border_soft);
  draw_chip(surface, chip3, theme.panel, theme.border_soft);
  render_text(surface, font, chip1.x + 10, chip1.y + 4, theme.text, "Enter Open");
  render_text(surface, font, chip2.x + 10, chip2.y + 4, theme.text, "Backspace Up");
  render_text(surface, font, chip3.x + 10, chip3.y + 4, theme.text, "Esc Cancel");

  char footer_text[128];
  snprintf(footer_text, sizeof(footer_text), "%d/%d", state->count > 0 ? state->selected_index + 1 : 0, state->count);
  render_text(surface, font, footer.x + footer.w - 64, footer.y + 8, theme.muted, footer_text);

  SDL_UpdateWindowSurface(window);
}

static int ends_with_ci(const char *name, const char *suffix) {
  size_t name_len = strlen(name);
  size_t suffix_len = strlen(suffix);
  if (name_len < suffix_len) return 0;

  const char *tail = name + (name_len - suffix_len);
  for (size_t i = 0; i < suffix_len; i++) {
    if (tolower((unsigned char)tail[i]) != tolower((unsigned char)suffix[i])) {
      return 0;
    }
  }
  return 1;
}

static int is_supported_image_name(const char *name) {
  return ends_with_ci(name, ".jpg") || ends_with_ci(name, ".jpeg") ||
         ends_with_ci(name, ".png") || ends_with_ci(name, ".tif") ||
         ends_with_ci(name, ".tiff");
}

static void free_image_list(ImageList *list) {
  if (!list || !list->items) return;
  for (int i = 0; i < list->count; i++) {
    free(list->items[i]);
  }
  free(list->items);
  list->items = NULL;
  list->count = 0;
}

static int cmp_paths(const void *a, const void *b) {
  const char *const *pa = a;
  const char *const *pb = b;
  return strcmp(*pa, *pb);
}

static int build_image_list(const char *filepath, ImageList *list, int *current_index, char **normalized_current_path) {
  memset(list, 0, sizeof(*list));
  *current_index = -1;
  *normalized_current_path = NULL;

  char *dir = path_dirname(filepath);
  if (!dir) return 0;

  const char *base = path_basename(filepath);
  char *current_path = join_path(dir, base);
  if (!current_path) {
    free(dir);
    return 0;
  }

  DIR *handle = opendir(dir);
  if (!handle) {
    free(dir);
    free(current_path);
    return 0;
  }

  struct dirent *entry;
  int capacity = 0;
  while ((entry = readdir(handle)) != NULL) {
    if (entry->d_name[0] == '.' &&
        (entry->d_name[1] == '\0' ||
         (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
      continue;
    }

    if (!is_supported_image_name(entry->d_name) && strcmp(entry->d_name, base) != 0) {
      continue;
    }

    char *full_path = join_path(dir, entry->d_name);
    if (!full_path) {
      closedir(handle);
      free(dir);
      free(current_path);
      free_image_list(list);
      return 0;
    }

    struct stat st;
    if (stat(full_path, &st) != 0 || !S_ISREG(st.st_mode)) {
      free(full_path);
      continue;
    }

    if (list->count == capacity) {
      int next_capacity = capacity == 0 ? 16 : capacity * 2;
      char **items = realloc(list->items, (size_t)next_capacity * sizeof(char *));
      if (!items) {
        free(full_path);
        closedir(handle);
        free(dir);
        free(current_path);
        free_image_list(list);
        return 0;
      }
      list->items = items;
      capacity = next_capacity;
    }

    list->items[list->count++] = full_path;
  }

  closedir(handle);
  free(dir);

  if (list->count == 0) {
    free(current_path);
    return 0;
  }

  qsort(list->items, (size_t)list->count, sizeof(char *), cmp_paths);

  for (int i = 0; i < list->count; i++) {
    if (strcmp(list->items[i], current_path) == 0) {
      *current_index = i;
      break;
    }
  }

  *normalized_current_path = current_path;
  return 1;
}

static SDL_Surface *load_surface_for_path(const char *path) {
  SDL_Surface *image = IMG_Load(path);
  if (!image) {
    fprintf(stderr, "IMG_Load error for %s: %s\n", path, IMG_GetError());
  }
  return image;
}

static void update_window_title(Viewer *viewer) {
  if (!viewer->window || viewer->list.count <= 0 || viewer->current_index < 0) return;

  const char *path = viewer->list.items[viewer->current_index];
  const char *name = path_basename(path);
  char title[512];
  snprintf(title, sizeof(title), "Image Viewer - %s (%d/%d)", name,
           viewer->current_index + 1, viewer->list.count);
  SDL_SetWindowTitle(viewer->window, title);
}

static void get_nav_buttons(int w, int h, SDL_Rect *prev, SDL_Rect *next) {
  const int button_w = 56;
  const int button_h = 80;
  const int margin = 18;

  prev->x = margin;
  prev->y = (h - button_h) / 2;
  prev->w = button_w;
  prev->h = button_h;

  next->x = w - margin - button_w;
  next->y = (h - button_h) / 2;
  next->w = button_w;
  next->h = button_h;
}

static void put_pixel(SDL_Surface *surface, int x, int y, Uint32 color) {
  if (x < 0 || y < 0 || x >= surface->w || y >= surface->h) return;

  Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * surface->format->BytesPerPixel;
  switch (surface->format->BytesPerPixel) {
    case 4:
      *(Uint32 *)p = color;
      break;
    case 3:
      if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
        p[0] = (color >> 16) & 0xFF;
        p[1] = (color >> 8) & 0xFF;
        p[2] = color & 0xFF;
      } else {
        p[0] = color & 0xFF;
        p[1] = (color >> 8) & 0xFF;
        p[2] = (color >> 16) & 0xFF;
      }
      break;
    default:
      break;
  }
}

static void draw_line(SDL_Surface *surface, int x0, int y0, int x1, int y1, Uint32 color) {
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  for (;;) {
    put_pixel(surface, x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static void draw_nav_button(SDL_Surface *surface, SDL_Rect rect, int direction, int enabled) {
  SlateTheme theme = slate_theme();
  SDL_Color fill = enabled ? theme.panel_alt : theme.panel;
  SDL_Color border = enabled ? theme.border : theme.border_soft;
  SDL_Color text = enabled ? theme.text : theme.muted;
  draw_button(surface, rect, fill, border);
  draw_chevron(surface, rect, direction, text);
}

static int point_in_rect(int x, int y, SDL_Rect rect) {
  return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

SDL_Surface *rotate_surface(SDL_Surface *src, int degrees) {
  if (degrees == 0) return src;

  SDL_Surface *converted = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
  if (!converted) return src;

  int src_w = converted->w;
  int src_h = converted->h;
  int dst_w = (degrees == 90 || degrees == 270) ? src_h : src_w;
  int dst_h = (degrees == 90 || degrees == 270) ? src_w : src_h;

  SDL_Surface *dst = SDL_CreateRGBSurface(0, dst_w, dst_h, 32,
                                           0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
  if (!dst) {
    SDL_FreeSurface(converted);
    return src;
  }

  SDL_LockSurface(converted);
  SDL_LockSurface(dst);

  Uint32 *src_pixels = (Uint32 *)converted->pixels;
  Uint32 *dst_pixels = (Uint32 *)dst->pixels;

  for (int y = 0; y < src_h; y++) {
    for (int x = 0; x < src_w; x++) {
      int dx, dy;
      if (degrees == 90) {
        dx = src_h - 1 - y;
        dy = x;
      } else if (degrees == 180) {
        dx = src_w - 1 - x;
        dy = src_h - 1 - y;
      } else {
        dx = y;
        dy = src_w - 1 - x;
      }
      dst_pixels[dy * dst_w + dx] = src_pixels[y * src_w + x];
    }
  }

  SDL_UnlockSurface(dst);
  SDL_UnlockSurface(converted);
  SDL_FreeSurface(converted);
  return dst;
}

static void render(Viewer *viewer) {
  SlateTheme theme = slate_theme();
  SDL_Window *pwindow = viewer->window;
  SDL_Surface *image = viewer->image;
  TTF_Font *font = viewer->ui_font;
  SDL_Rect display;
  SDL_GetDisplayBounds(0, &display);

  if (!font) return;

  SDL_Surface *rotated = rotate_surface(image, viewer->rotation);

  int img_w = rotated->w;
  int img_h = rotated->h;

  float fit_scale = 1.0;
  int chrome_w = 56;
  int chrome_h = 184;
  int available_w = display.w - chrome_w;
  int available_h = display.h - chrome_h;
  if (available_w < 320) available_w = 320;
  if (available_h < 240) available_h = 240;

  if (img_w > available_w || img_h > available_h) {
    float scale_w = (float)available_w / img_w;
    float scale_h = (float)available_h / img_h;
    fit_scale = (scale_w < scale_h) ? scale_w : scale_h;
  }

  int render_w = (int)(img_w * fit_scale * viewer->zoom);
  int render_h = (int)(img_h * fit_scale * viewer->zoom);
  if (render_w < 1) render_w = 1;
  if (render_h < 1) render_h = 1;

  if (viewer->fullscreen) {
    SDL_SetWindowSize(pwindow, display.w, display.h);
  } else {
    int window_w = render_w + chrome_w;
    int window_h = render_h + chrome_h;
    if (window_w > display.w) window_w = display.w;
    if (window_h > display.h) window_h = display.h;
    if (window_w < 720) window_w = 720;
    if (window_h < 540) window_h = 540;
    SDL_SetWindowSize(pwindow, window_w, window_h);
  }

  SDL_Surface *psurface = SDL_GetWindowSurface(pwindow);
  fill_vertical_gradient(psurface, theme.bg0, theme.bg1);

  SDL_Rect outer = {16, 16, psurface->w - 32, psurface->h - 32};
  draw_panel(psurface, outer, theme.panel, theme.border);

  SDL_Rect header = {outer.x + 16, outer.y + 16, outer.w - 32, 72};
  fill_horizontal_gradient(psurface, header, theme.panel_alt, theme.panel_soft);
  draw_border(psurface, header, theme.border_soft);

  const char *name = path_basename(viewer->list.items[viewer->current_index]);
  char title[512];
  char subtitle[1024];
  char info_text[128];
  snprintf(title, sizeof(title), "%s", name);
  SDL_Rect title_box = {header.x + 18, header.y + 12, header.w - 250, 24};
  SDL_Rect subtitle_box = {header.x + 18, header.y + 40, header.w - 250, 20};
  snprintf(subtitle, sizeof(subtitle), "%s", viewer->list.items[viewer->current_index]);
  render_text(psurface, font, title_box.x, title_box.y, theme.text, title);
  render_text(psurface, font, subtitle_box.x, subtitle_box.y, theme.muted, subtitle);

  SDL_Rect info_badge = {header.x + header.w - 214, header.y + 18, 194, 30};
  draw_chip(psurface, info_badge, theme.panel, theme.border_soft);
  snprintf(info_text, sizeof(info_text), "%d / %d", viewer->current_index + 1, viewer->list.count);
  render_text(psurface, font, info_badge.x + 16, info_badge.y + 6, theme.text, info_text);

  SDL_Rect content = {outer.x + 16, header.y + header.h + 12, outer.w - 32,
                      outer.h - (header.h + 12) - 86};
  draw_panel(psurface, content, theme.panel_alt, theme.border_soft);

  SDL_Rect image_pad = {content.x + 20, content.y + 20, content.w - 40, content.h - 40};
  int offset_x = image_pad.x + (image_pad.w - render_w) / 2;
  int offset_y = image_pad.y + (image_pad.h - render_h) / 2;
  if (offset_x < image_pad.x) offset_x = image_pad.x;
  if (offset_y < image_pad.y) offset_y = image_pad.y;
  SDL_Rect dst = {offset_x, offset_y, render_w, render_h};

  SDL_Rect image_border = {image_pad.x - 1, image_pad.y - 1, image_pad.w + 2, image_pad.h + 2};
  draw_panel(psurface, image_border, theme.panel, theme.border_soft);
  SDL_Rect image_bg = {image_pad.x, image_pad.y, image_pad.w, image_pad.h};
  SDL_FillRect(psurface, &image_bg, SDL_MapRGB(psurface->format, 2, 6, 23));
  SDL_BlitScaled(rotated, NULL, psurface, &dst);

  SDL_Rect prev_button = {content.x + 16, content.y + content.h / 2 - 28, 56, 56};
  SDL_Rect next_button = {content.x + content.w - 72, content.y + content.h / 2 - 28, 56, 56};
  int enabled = viewer->list.count > 1;
  draw_nav_button(psurface, prev_button, -1, enabled);
  draw_nav_button(psurface, next_button, 1, enabled);

  SDL_Rect footer = {outer.x + 16, outer.y + outer.h - 48, outer.w - 32, 32};
  draw_panel(psurface, footer, theme.panel_alt, theme.border_soft);
  SDL_Rect chip1 = {footer.x + 14, footer.y + 5, 124, 22};
  SDL_Rect chip2 = {chip1.x + chip1.w + 10, footer.y + 5, 90, 22};
  SDL_Rect chip3 = {chip2.x + chip2.w + 10, footer.y + 5, 96, 22};
  SDL_Rect chip4 = {chip3.x + chip3.w + 10, footer.y + 5, 76, 22};
  draw_chip(psurface, chip1, theme.panel, theme.border_soft);
  draw_chip(psurface, chip2, theme.panel, theme.border_soft);
  draw_chip(psurface, chip3, theme.panel, theme.border_soft);
  draw_chip(psurface, chip4, theme.panel, theme.border_soft);
  render_text(psurface, font, chip1.x + 10, chip1.y + 4, theme.text, "Left / Right");
  render_text(psurface, font, chip2.x + 10, chip2.y + 4, theme.text, "R Rotate");
  render_text(psurface, font, chip3.x + 10, chip3.y + 4, theme.text, "+ / - Zoom");
  render_text(psurface, font, chip4.x + 10, chip4.y + 4, theme.text, "O Open");

  char status[128];
  snprintf(status, sizeof(status), "%dx%d", img_w, img_h);
  render_text(psurface, font, footer.x + footer.w - 86, footer.y + 4, theme.muted, status);

  SDL_UpdateWindowSurface(pwindow);

  if (rotated != image) {
    SDL_FreeSurface(rotated);
  }
}

static int load_viewer_image(Viewer *viewer, int index) {
  SDL_Surface *next = load_surface_for_path(viewer->list.items[index]);
  if (!next) return 0;

  if (viewer->image) {
    SDL_FreeSurface(viewer->image);
  }

  viewer->image = next;
  viewer->current_index = index;
  viewer->rotation = 0;
  viewer->zoom = 1.0f;
  update_window_title(viewer);
  render(viewer);
  return 1;
}

static int navigate_viewer(Viewer *viewer, int delta) {
  if (viewer->list.count <= 1) return 1;

  int next = viewer->current_index;
  for (int attempts = 0; attempts < viewer->list.count; attempts++) {
    next = (next + delta + viewer->list.count) % viewer->list.count;
    if (load_viewer_image(viewer, next)) {
      return 1;
    }
  }

  fprintf(stderr, "No loadable image found in directory after navigation.\n");
  return 0;
}

static void cleanup_viewer(Viewer *viewer) {
  if (viewer->image) {
    SDL_FreeSurface(viewer->image);
    viewer->image = NULL;
  }
  free_image_list(&viewer->list);
}

static char *pick_image_path(Viewer *viewer, const char *current_path) {
  BrowserState browser;
  memset(&browser, 0, sizeof(browser));

  if (!viewer || !viewer->ui_font) {
    return NULL;
  }

  const char *start_dir = NULL;
  char *derived_dir = NULL;
  if (current_path && current_path[0] != '\0') {
    derived_dir = path_dirname(current_path);
    start_dir = derived_dir;
  } else {
    const char *home = getenv("HOME");
    if (home && home[0] != '\0') {
      start_dir = home;
    } else {
      start_dir = ".";
    }
  }

  if (!browser_set_dir(&browser, start_dir)) {
    free(derived_dir);
    return NULL;
  }
  free(derived_dir);

  int dirty = 1;
  char *selected_path = NULL;
  int running = 1;

  while (running) {
    if (dirty) {
      render_browser(viewer->window, viewer->ui_font, &browser);
      dirty = 0;
    }

    SDL_Event event;
    if (!SDL_WaitEventTimeout(&event, 50)) {
      continue;
    }

    do {
      if (event.type == SDL_QUIT) {
        running = 0;
        break;
      } else if (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        dirty = 1;
      } else if (event.type == SDL_KEYDOWN) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(viewer->window, &w, &h);
        int visible = browser_visible_rows(h, TTF_FontHeight(viewer->ui_font) + 10, 124, 56);

        switch (event.key.keysym.sym) {
          case SDLK_ESCAPE:
            running = 0;
            break;
          case SDLK_UP:
            browser_move_selection(&browser, -1, visible);
            dirty = 1;
            break;
          case SDLK_DOWN:
            browser_move_selection(&browser, 1, visible);
            dirty = 1;
            break;
          case SDLK_PAGEUP:
            browser_move_selection(&browser, -visible, visible);
            dirty = 1;
            break;
          case SDLK_PAGEDOWN:
            browser_move_selection(&browser, visible, visible);
            dirty = 1;
            break;
          case SDLK_HOME:
            browser_jump_to(&browser, 0, visible);
            dirty = 1;
            break;
          case SDLK_END:
            browser_jump_to(&browser, browser.count - 1, visible);
            dirty = 1;
            break;
          case SDLK_BACKSPACE:
          case SDLK_LEFT:
            if (browser.current_dir && strcmp(browser.current_dir, "/") != 0) {
              char *parent_dir = path_dirname(browser.current_dir);
              if (parent_dir && browser_set_dir(&browser, parent_dir)) {
                dirty = 1;
              }
              free(parent_dir);
            }
            break;
          case SDLK_RETURN:
          case SDLK_KP_ENTER: {
            char *picked = browser_activate_selected(&browser);
            if (picked) {
              selected_path = picked;
              running = 0;
            } else {
              dirty = 1;
            }
            break;
          }
          default:
            break;
        }
      } else if (event.type == SDL_MOUSEWHEEL) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(viewer->window, &w, &h);
        int visible = browser_visible_rows(h, TTF_FontHeight(viewer->ui_font) + 10, 124, 56);
        browser_move_selection(&browser, -event.wheel.y, visible);
        dirty = 1;
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(viewer->window, &w, &h);
        int row_h = TTF_FontHeight(viewer->ui_font) + 10;
        int top = 124;
        int idx = browser.scroll_offset + ((event.button.y - top) / row_h);
        if (event.button.y >= top && idx >= 0 && idx < browser.count) {
          browser.selected_index = idx;
          browser_ensure_visible(&browser, browser_visible_rows(h, row_h, top, 56));
          char *picked = browser_activate_selected(&browser);
          if (picked) {
            selected_path = picked;
            running = 0;
          } else {
            dirty = 1;
          }
        }
      }
    } while (SDL_PollEvent(&event));
  }

  free_browser_state(&browser);
  return selected_path;
}

static int load_viewer_from_path(Viewer *viewer, const char *filepath) {
  ImageList next_list;
  int next_index = -1;
  char *normalized_current_path = NULL;
  SDL_Surface *next_image = NULL;
  SDL_Surface *old_image = NULL;
  ImageList old_list = viewer->list;

  if (!build_image_list(filepath, &next_list, &next_index, &normalized_current_path)) {
    return 0;
  }

  next_image = load_surface_for_path(normalized_current_path);
  if (!next_image) {
    free(normalized_current_path);
    free_image_list(&next_list);
    return 0;
  }

  old_image = viewer->image;
  viewer->image = next_image;
  viewer->list = next_list;
  viewer->current_index = next_index;
  viewer->rotation = 0;
  viewer->zoom = 1.0f;

  if (old_image) {
    SDL_FreeSurface(old_image);
  }
  free_image_list(&old_list);
  free(normalized_current_path);

  update_window_title(viewer);
  if (viewer->window) {
    render(viewer);
  }
  return 1;
}

int main(int argc, char *argv[]) {
  Viewer viewer;
  memset(&viewer, 0, sizeof(viewer));

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
    return 1;
  }

  if (!(IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_TIF) &
        (IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_TIF))) {
    fprintf(stderr, "IMG_Init error: %s\n", IMG_GetError());
    SDL_Quit();
    return 1;
  }

  if (TTF_Init() != 0) {
    fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError());
    IMG_Quit();
    SDL_Quit();
    return 1;
  }

  const char *font_path = find_ui_font_path();
  viewer.ui_font = font_path ? TTF_OpenFont(font_path, 20) : NULL;
  if (!viewer.ui_font) {
    fprintf(stderr, "Could not load UI font\n");
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 1;
  }

  char *picked_path = NULL;
  if (argc > 1) {
    picked_path = dup_string(argv[1]);
    if (!picked_path) {
      TTF_CloseFont(viewer.ui_font);
      TTF_Quit();
      IMG_Quit();
      SDL_Quit();
      return 1;
    }
  } else {
    SDL_Rect display;
    SDL_GetDisplayBounds(0, &display);
    int browser_w = display.w > 1200 ? 1200 : display.w - 100;
    int browser_h = display.h > 900 ? 900 : display.h - 100;
    if (browser_w < 640) browser_w = 640;
    if (browser_h < 480) browser_h = 480;

    viewer.window = SDL_CreateWindow("Image Viewer - Open Image", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, browser_w, browser_h, 0);
    if (!viewer.window) {
      fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
      TTF_CloseFont(viewer.ui_font);
      TTF_Quit();
      IMG_Quit();
      SDL_Quit();
      return 1;
    }

    picked_path = pick_image_path(&viewer, NULL);
    if (!picked_path) {
      SDL_DestroyWindow(viewer.window);
      viewer.window = NULL;
      TTF_CloseFont(viewer.ui_font);
      TTF_Quit();
      IMG_Quit();
      SDL_Quit();
      return 0;
    }
  }

  if (!file_exists_regular(picked_path)) {
    fprintf(stderr, "Could not open image path %s\n", picked_path);
    free(picked_path);
    if (viewer.window) SDL_DestroyWindow(viewer.window);
    TTF_CloseFont(viewer.ui_font);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 1;
  }

  if (!load_viewer_from_path(&viewer, picked_path)) {
    fprintf(stderr, "Could not build image list for %s\n", picked_path);
    free(picked_path);
    if (viewer.window) SDL_DestroyWindow(viewer.window);
    TTF_CloseFont(viewer.ui_font);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 1;
  }

  if (!viewer.window) {
    SDL_Rect display;
    SDL_GetDisplayBounds(0, &display);

    int win_w = viewer.image->w;
    int win_h = viewer.image->h;

    if (win_w > display.w || win_h > display.h) {
      float scale_w = (float)display.w / win_w;
      float scale_h = (float)display.h / win_h;
      float scale = (scale_w < scale_h) ? scale_w : scale_h;
      win_w = (int)(win_w * scale);
      win_h = (int)(win_h * scale);
    }

    viewer.window =
        SDL_CreateWindow("Image Viewer", SDL_WINDOWPOS_CENTERED,
                         SDL_WINDOWPOS_CENTERED, win_w, win_h, 0);
    if (!viewer.window) {
      fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
      cleanup_viewer(&viewer);
      free(picked_path);
      TTF_CloseFont(viewer.ui_font);
      TTF_Quit();
      IMG_Quit();
      SDL_Quit();
      return 1;
    }
  }

  viewer.rotation = 0;
  viewer.zoom = 1.0f;
  viewer.fullscreen = 0;
  update_window_title(&viewer);

  free(picked_path);

  render(&viewer);

  SDL_Event event;
  int running = 1;
  while (running) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = 0;
      } else if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_q) {
          running = 0;
        } else if (event.key.keysym.sym == SDLK_f) {
          viewer.fullscreen = !viewer.fullscreen;
          if (viewer.fullscreen) {
            SDL_SetWindowFullscreen(viewer.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
          } else {
            SDL_SetWindowFullscreen(viewer.window, 0);
          }
          render(&viewer);
        } else if (event.key.keysym.sym == SDLK_LEFT) {
          navigate_viewer(&viewer, -1);
        } else if (event.key.keysym.sym == SDLK_RIGHT) {
          navigate_viewer(&viewer, 1);
        } else if (event.key.keysym.sym == SDLK_r) {
          viewer.rotation = (viewer.rotation + 90) % 360;
          render(&viewer);
        } else if (event.key.keysym.sym == SDLK_EQUALS || event.key.keysym.sym == SDLK_PLUS) {
          viewer.zoom *= 1.2f;
          render(&viewer);
        } else if (event.key.keysym.sym == SDLK_MINUS) {
          viewer.zoom *= 0.8f;
          if (viewer.zoom < 0.1f) viewer.zoom = 0.1f;
          render(&viewer);
        } else if (event.key.keysym.sym == SDLK_0) {
          viewer.zoom = 1.0f;
          render(&viewer);
        } else if (event.key.keysym.sym == SDLK_o) {
          char *new_path = pick_image_path(&viewer,
                                           viewer.list.count > 0 && viewer.current_index >= 0
                                               ? viewer.list.items[viewer.current_index]
                                               : NULL);
          if (new_path) {
            if (!file_exists_regular(new_path)) {
              fprintf(stderr, "Could not open image path %s\n", new_path);
            } else if (!load_viewer_from_path(&viewer, new_path)) {
              fprintf(stderr, "Could not load image path %s\n", new_path);
            }
            free(new_path);
          }
        }
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(viewer.window, &w, &h);
        SDL_Rect prev_button;
        SDL_Rect next_button;
        get_nav_buttons(w, h, &prev_button, &next_button);

        if (point_in_rect(event.button.x, event.button.y, prev_button)) {
          navigate_viewer(&viewer, -1);
        } else if (point_in_rect(event.button.x, event.button.y, next_button)) {
          navigate_viewer(&viewer, 1);
        }
      }
    }
  }

  cleanup_viewer(&viewer);
  TTF_CloseFont(viewer.ui_font);
  TTF_Quit();
  IMG_Quit();
  SDL_Quit();
  return 0;
}
