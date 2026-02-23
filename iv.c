#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>
#include <stdio.h>

int main() {
  const int width = 800, height = 900;

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
    return 1;
  }

  if (!(IMG_Init(IMG_INIT_JPG) & IMG_INIT_JPG)) {
    fprintf(stderr, "IMG_Init error: %s\n", IMG_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Window *pwindow =
      SDL_CreateWindow("Image Viewer", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, width, height, 0);

  SDL_Surface *psurface = SDL_GetWindowSurface(pwindow);
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      SDL_Quit();
      exit(0);
    }
  }
  SDL_Surface *image = IMG_Load("/home/shreyanshxyz/Downloads/i.jpg");
  if (image == NULL) {
    fprintf(stderr, "IMG_Load error: %s\n", IMG_GetError());
    IMG_Quit();
    SDL_Quit();
    return 1;
  }

  SDL_SetWindowSize(pwindow, image->w, image->h);
  psurface = SDL_GetWindowSurface(pwindow);
  SDL_BlitSurface(image, NULL, psurface, NULL);
  SDL_UpdateWindowSurface(pwindow);

  SDL_Delay(5000);

  SDL_FreeSurface(image);
  IMG_Quit();
  SDL_Quit();
}
