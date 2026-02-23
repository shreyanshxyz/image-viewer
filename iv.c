#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  const char *filepath = (argc > 1) ? argv[1] : "/home/shreyanshxyz/Downloads/i.jpg";

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
    return 1;
  }

  if (!(IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG) & (IMG_INIT_JPG | IMG_INIT_PNG))) {
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

  SDL_Surface *psurface = SDL_GetWindowSurface(pwindow);

  SDL_FillRect(psurface, NULL, SDL_MapRGB(psurface->format, 0, 0, 0));

  int offset_x = (psurface->w - win_w) / 2;
  int offset_y = (psurface->h - win_h) / 2;
  SDL_Rect dst = {offset_x, offset_y, win_w, win_h};
  SDL_BlitScaled(image, NULL, psurface, &dst);
  SDL_UpdateWindowSurface(pwindow);

  SDL_Event event;
  int running = 1;
  int fullscreen = 0;
  int orig_w = win_w;
  int orig_h = win_h;

  while (running) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = 0;
      } else if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_f) {
          fullscreen = !fullscreen;
          if (fullscreen) {
            SDL_SetWindowFullscreen(pwindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
            SDL_GetDisplayBounds(0, &display);
            SDL_SetWindowSize(pwindow, display.w, display.h);
            if (image->w <= display.w && image->h <= display.h) {
              win_w = image->w;
              win_h = image->h;
            } else {
              float scale_w = (float)display.w / image->w;
              float scale_h = (float)display.h / image->h;
              float scale = (scale_w < scale_h) ? scale_w : scale_h;
              win_w = (int)(image->w * scale);
              win_h = (int)(image->h * scale);
            }
          } else {
            SDL_SetWindowFullscreen(pwindow, 0);
            win_w = orig_w;
            win_h = orig_h;
            SDL_SetWindowSize(pwindow, win_w, win_h);
          }
          psurface = SDL_GetWindowSurface(pwindow);
          SDL_FillRect(psurface, NULL, SDL_MapRGB(psurface->format, 0, 0, 0));
          int offset_x = (psurface->w - win_w) / 2;
          int offset_y = (psurface->h - win_h) / 2;
          dst = (SDL_Rect){offset_x, offset_y, win_w, win_h};
          SDL_BlitScaled(image, NULL, psurface, &dst);
          SDL_UpdateWindowSurface(pwindow);
        }
      }
    }
  }

  SDL_FreeSurface(image);
  IMG_Quit();
  SDL_Quit();
}
