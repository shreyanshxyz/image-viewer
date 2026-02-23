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

  SDL_Window *pwindow =
      SDL_CreateWindow("Image Viewer", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, image->w, image->h, 0);

  SDL_Surface *psurface = SDL_GetWindowSurface(pwindow);
  SDL_BlitSurface(image, NULL, psurface, NULL);
  SDL_UpdateWindowSurface(pwindow);

  SDL_Event event;
  int running = 1;
  int fullscreen = 0;
  while (running) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = 0;
      } else if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_f) {
          fullscreen = !fullscreen;
          SDL_SetWindowFullscreen(pwindow, fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
          SDL_UpdateWindowSurface(pwindow);
        }
      }
    }
  }

  SDL_FreeSurface(image);
  IMG_Quit();
  SDL_Quit();
}
