#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

int main(int argc, char *argv[]) {
  const char *filepath = (argc > 1) ? argv[1] : "/home/shreyanshxyz/Downloads/i.jpg";
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

  char *normalized_current_path = NULL;
  if (!build_image_list(filepath, &viewer.list, &viewer.current_index, &normalized_current_path)) {
    fprintf(stderr, "Could not build image list for %s\n", filepath);
    IMG_Quit();
    SDL_Quit();
    return 1;
  }

  viewer.image = load_surface_for_path(normalized_current_path);
  if (viewer.image == NULL) {
    free(normalized_current_path);
    free_image_list(&viewer.list);
    IMG_Quit();
    SDL_Quit();
    return 1;
  }

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

  SDL_Window *pwindow =
      SDL_CreateWindow("Image Viewer", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, win_w, win_h, 0);
  if (!pwindow) {
    fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
    cleanup_viewer(&viewer);
    IMG_Quit();
    SDL_Quit();
    return 1;
  }
  viewer.window = pwindow;
  viewer.rotation = 0;
  viewer.zoom = 1.0f;
  viewer.fullscreen = 0;
  update_window_title(&viewer);

  free(normalized_current_path);

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
            SDL_SetWindowFullscreen(pwindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
          } else {
            SDL_SetWindowFullscreen(pwindow, 0);
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
        }
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(pwindow, &w, &h);
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
  IMG_Quit();
  SDL_Quit();
  return 0;
}
