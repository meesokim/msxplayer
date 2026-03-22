
#include <SDL2/SDL.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    SDL_SetMainReady();
    printf("SDL2 Test: Starting...\n"); fflush(stdout);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError()); fflush(stderr);
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("SDL2 Test Window", 
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
                                          640, 480, 
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError()); fflush(stderr);
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError()); fflush(stderr);
        return 1;
    }

    printf("Window created. You should see a RED window.\n");
    printf("Press ESC or close window to exit.\n"); fflush(stdout);

    SDL_RaiseWindow(window);

    bool quit = false;
    SDL_Event e;
    while (!quit) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) quit = true;
            }
        }

        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("SDL2 Test: Success.\n"); fflush(stdout);
    return 0;
}
