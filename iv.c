#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>

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

void render(SDL_Window *pwindow, SDL_Surface *image, int rotation, float zoom, int fullscreen) {
  SDL_Rect display;
  SDL_GetDisplayBounds(0, &display);

  SDL_Surface *rotated = rotate_surface(image, rotation);

  int img_w = rotated->w;
  int img_h = rotated->h;

  float fit_scale = 1.0;
  if (img_w > display.w || img_h > display.h) {
    float scale_w = (float)display.w / img_w;
    float scale_h = (float)display.h / img_h;
    fit_scale = (scale_w < scale_h) ? scale_w : scale_h;
  }

  int render_w = (int)(img_w * fit_scale * zoom);
  int render_h = (int)(img_h * fit_scale * zoom);

  if (fullscreen) {
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
  SDL_UpdateWindowSurface(pwindow);

  if (rotated != image) {
    SDL_FreeSurface(rotated);
  }
}

int main(int argc, char *argv[]) {
  const char *filepath = (argc > 1) ? argv[1] : "/home/shreyanshxyz/Downloads/i.jpg";

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

  SDL_Surface *image = IMG_Load(filepath);
  if (image == NULL) {
    fprintf(stderr, "IMG_Load error: %s\n", IMG_GetError());
    IMG_Quit();
    SDL_Quit();
    return 1;
  }

  SDL_Rect display;
  SDL_GetDisplayBounds(0, &display);

  int win_w = image->w;
  int win_h = image->h;

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

  int rotation = 0;
  float zoom = 1.0;
  int fullscreen = 0;

  render(pwindow, image, rotation, zoom, fullscreen);

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
          fullscreen = !fullscreen;
          if (fullscreen) {
            SDL_SetWindowFullscreen(pwindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
          } else {
            SDL_SetWindowFullscreen(pwindow, 0);
          }
          render(pwindow, image, rotation, zoom, fullscreen);
        } else if (event.key.keysym.sym == SDLK_r) {
          rotation = (rotation + 90) % 360;
          render(pwindow, image, rotation, zoom, fullscreen);
        } else if (event.key.keysym.sym == SDLK_EQUALS || event.key.keysym.sym == SDLK_PLUS) {
          zoom *= 1.2;
          render(pwindow, image, rotation, zoom, fullscreen);
        } else if (event.key.keysym.sym == SDLK_MINUS) {
          zoom *= 0.8;
          if (zoom < 0.1) zoom = 0.1;
          render(pwindow, image, rotation, zoom, fullscreen);
        } else if (event.key.keysym.sym == SDLK_0) {
          zoom = 1.0;
          render(pwindow, image, rotation, zoom, fullscreen);
        }
      }
    }
  }

  SDL_FreeSurface(image);
  IMG_Quit();
  SDL_Quit();
}
