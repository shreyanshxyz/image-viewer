#include <SDL2/SDL.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
  printf("Hello World!\n");

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window *pwindow = SDL_CreateWindow("Image Viewer", SDL_WINDOWPOS_CENTERED,
                                         SDL_WINDOWPOS_CENTERED, 800, 600, 0);

  SDL_Surface *psurface = SDL_GetWindowSurface(pwindow);

  Uint8 r, g, b;
  r = 0x1A;
  g = b = 0;
  Uint32 color = SDL_MapRGB(psurface->format, r, g, b);
  SDL_FillRect(psurface, NULL, color);

  SDL_UpdateWindowSurface(pwindow);

  SDL_Delay(1000);
}
