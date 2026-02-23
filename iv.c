#include <SDL2/SDL.h>
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

  SDL_FillRect(psurface, NULL, 0xFF0000);

  SDL_UpdateWindowSurface(pwindow);

  SDL_Delay(1000);
}
