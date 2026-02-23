#include <SDL2/SDL.h>
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

  printf("Video driver: %s\n", SDL_GetCurrentVideoDriver());

  SDL_Renderer *renderer = SDL_CreateRenderer(pwindow, -1, 0);
  if (renderer) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
  }

  Uint32 start = SDL_GetTicks();
  SDL_Event e;
  while (SDL_GetTicks() - start < 3000) {
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        goto done;
      }
    }
    SDL_Delay(16);
  }

done:
  if (renderer)
    SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(pwindow);
  SDL_Quit();
  return 0;
}
