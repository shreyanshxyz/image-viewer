#include <SDL2/SDL_video.h>
#include<stdio.h>
#include<stdlib.h>
#include<SDL2/SDL.h>

int main() {
  printf("Hello World!\n");

  SDL_Window *pwindow = SDL_CreateWindow("Image Viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, 0);
}

