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

static void render_browser(SDL_Window *window, TTF_Font *font, BrowserState *state) {
  int w = 0;
  int h = 0;
  SDL_GetWindowSize(window, &w, &h);
  SDL_Surface *surface = SDL_GetWindowSurface(window);
  SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 18, 18, 22));

  SDL_Color text = {235, 235, 235, 255};
  SDL_Color dim = {160, 160, 160, 255};
  SDL_Color row_text = {240, 240, 240, 255};
  SDL_Color row_dim = {190, 190, 190, 255};

  SDL_Rect header = {24, 18, w - 48, 56};
  SDL_FillRect(surface, &header, SDL_MapRGB(surface->format, 30, 34, 45));
  render_text(surface, font, 36, 30, text, "Open Image");
  render_text(surface, font, 36, 54, dim, "Enter or click: open  Backspace: parent  Esc: cancel");

  char path_line[1024];
  snprintf(path_line, sizeof(path_line), "%s", state->current_dir ? state->current_dir : ".");
  render_text(surface, font, 24, 86, text, path_line);

  int top = 124;
  int bottom = 56;
  int row_h = TTF_FontHeight(font) + 10;
  int visible = browser_visible_rows(h, row_h, top, bottom);
  browser_ensure_visible(state, visible);

  if (state->count == 0) {
    render_text(surface, font, 24, top + 10, dim, "No image files found in this folder.");
  }

  for (int i = state->scroll_offset; i < state->count && i < state->scroll_offset + visible; i++) {
    int y = top + (i - state->scroll_offset) * row_h;
    SDL_Rect row = {20, y, w - 40, row_h - 4};
    if (i == state->selected_index) {
      SDL_FillRect(surface, &row, SDL_MapRGB(surface->format, 70, 90, 130));
    } else {
      SDL_FillRect(surface, &row, SDL_MapRGB(surface->format, 28, 28, 34));
    }

    BrowserEntry *entry = &state->entries[i];
    render_text(surface, font, 34, y + 4, entry->is_dir ? text : row_text, entry->is_dir ? "[DIR] " : "[IMG] ");
    render_text(surface, font, 92, y + 4, entry->is_dir ? text : row_dim, entry->display_name);
  }

  SDL_Rect footer = {20, h - 40, w - 40, 24};
  SDL_FillRect(surface, &footer, SDL_MapRGB(surface->format, 24, 24, 28));
  char footer_text[512];
  snprintf(footer_text, sizeof(footer_text), "%d items  %d/%d", state->count, state->selected_index + 1, state->count > 0 ? state->count : 0);
  render_text(surface, font, 30, h - 34, dim, footer_text);

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

static void draw_arrow(SDL_Surface *surface, SDL_Rect rect, int direction, Uint32 color) {
  int cy = rect.y + rect.h / 2;
  int top = rect.y + 20;
  int bottom = rect.y + rect.h - 20;
  int left = rect.x + 18;
  int right = rect.x + rect.w - 18;

  if (direction < 0) {
    draw_line(surface, right, top, left, cy, color);
    draw_line(surface, left, cy, right, bottom, color);
  } else {
    draw_line(surface, left, top, right, cy, color);
    draw_line(surface, right, cy, left, bottom, color);
  }
}

static void draw_nav_button(SDL_Surface *surface, SDL_Rect rect, int direction, int enabled) {
  Uint32 bg = SDL_MapRGB(surface->format, enabled ? 52 : 32, enabled ? 52 : 32, enabled ? 52 : 32);
  Uint32 border = SDL_MapRGB(surface->format, enabled ? 235 : 120, enabled ? 235 : 120, enabled ? 235 : 120);
  SDL_FillRect(surface, &rect, bg);

  SDL_Rect top = {rect.x, rect.y, rect.w, 2};
  SDL_Rect bottom = {rect.x, rect.y + rect.h - 2, rect.w, 2};
  SDL_Rect left = {rect.x, rect.y, 2, rect.h};
  SDL_Rect right = {rect.x + rect.w - 2, rect.y, 2, rect.h};
  SDL_FillRect(surface, &top, border);
  SDL_FillRect(surface, &bottom, border);
  SDL_FillRect(surface, &left, border);
  SDL_FillRect(surface, &right, border);

  draw_arrow(surface, rect, direction, border);
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
  SDL_Window *pwindow = viewer->window;
  SDL_Surface *image = viewer->image;
  SDL_Rect display;
  SDL_GetDisplayBounds(0, &display);

  SDL_Surface *rotated = rotate_surface(image, viewer->rotation);

  int img_w = rotated->w;
  int img_h = rotated->h;

  float fit_scale = 1.0;
  if (img_w > display.w || img_h > display.h) {
    float scale_w = (float)display.w / img_w;
    float scale_h = (float)display.h / img_h;
    fit_scale = (scale_w < scale_h) ? scale_w : scale_h;
  }

  int render_w = (int)(img_w * fit_scale * viewer->zoom);
  int render_h = (int)(img_h * fit_scale * viewer->zoom);
  if (render_w < 1) render_w = 1;
  if (render_h < 1) render_h = 1;

  if (viewer->fullscreen) {
    SDL_SetWindowSize(pwindow, display.w, display.h);
  } else {
    SDL_SetWindowSize(pwindow, render_w, render_h);
  }

  SDL_Surface *psurface = SDL_GetWindowSurface(pwindow);
  SDL_FillRect(psurface, NULL, SDL_MapRGB(psurface->format, 0, 0, 0));

  int offset_x = (psurface->w - render_w) / 2;
  int offset_y = (psurface->h - render_h) / 2;
  SDL_Rect dst = {offset_x, offset_y, render_w, render_h};
  SDL_BlitScaled(rotated, NULL, psurface, &dst);

  SDL_Rect prev_button;
  SDL_Rect next_button;
  get_nav_buttons(psurface->w, psurface->h, &prev_button, &next_button);
  int enabled = viewer->list.count > 1;
  draw_nav_button(psurface, prev_button, -1, enabled);
  draw_nav_button(psurface, next_button, 1, enabled);

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

  TTF_Font *font = NULL;
  const char *font_path = find_ui_font_path();
  if (font_path) {
    font = TTF_OpenFont(font_path, 20);
  }
  if (!font) {
    free(derived_dir);
    return NULL;
  }

  if (!browser_set_dir(&browser, start_dir)) {
    TTF_CloseFont(font);
    free(derived_dir);
    return NULL;
  }
  free(derived_dir);

  int dirty = 1;
  char *selected_path = NULL;
  int running = 1;

  while (running) {
    if (dirty) {
      render_browser(viewer->window, font, &browser);
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
        int visible = browser_visible_rows(h, TTF_FontHeight(font) + 10, 124, 56);

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
        int visible = browser_visible_rows(h, TTF_FontHeight(font) + 10, 124, 56);
        browser_move_selection(&browser, -event.wheel.y, visible);
        dirty = 1;
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(viewer->window, &w, &h);
        int row_h = TTF_FontHeight(font) + 10;
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

  TTF_CloseFont(font);
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

  char *picked_path = NULL;
  if (argc > 1) {
    picked_path = dup_string(argv[1]);
    if (!picked_path) {
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
      TTF_Quit();
      IMG_Quit();
      SDL_Quit();
      return 1;
    }

    picked_path = pick_image_path(&viewer, NULL);
    if (!picked_path) {
      SDL_DestroyWindow(viewer.window);
      viewer.window = NULL;
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
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 1;
  }

  if (!load_viewer_from_path(&viewer, picked_path)) {
    fprintf(stderr, "Could not build image list for %s\n", picked_path);
    free(picked_path);
    if (viewer.window) SDL_DestroyWindow(viewer.window);
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
  TTF_Quit();
  IMG_Quit();
  SDL_Quit();
  return 0;
}
