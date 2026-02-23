#include <SDL2/SDL.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
  const int width = 800, height = 900;

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window *pwindow =
      SDL_CreateWindow("Image Viewer", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, width, height, 0);

  SDL_Surface *psurface = SDL_GetWindowSurface(pwindow);

  Uint8 r, g, b;
  r = 0xFF;
  g = b = 0;
  Uint32 color = SDL_MapRGB(psurface->format, r, g, b);

  SDL_Rect pixel = (SDL_Rect){0, 0, 1, 1};
  for (int x = 0; x < width; x++) {
    for (int y = 0; y < height; y++) {
      pixel.x = x;
      pixel.y = y;
      SDL_FillRect(psurface, &pixel, color);
    }
  }

  SDL_UpdateWindowSurface(pwindow);

  SDL_Delay(1000);
}
