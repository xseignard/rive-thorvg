/// Desktop SDL example — primary development tool for rive-thorvg.
///
/// Usage: ./rive_sdl_example [path/to/file.riv]
///
/// Opens a 640x480 window and renders the .riv file with mouse interaction.
/// This is the most-used tool throughout development — iterate in seconds,
/// not minutes.

#include "rive_thorvg.h"
#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// File loading helper
// ---------------------------------------------------------------------------

static uint8_t* load_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) {
        fclose(f);
        fprintf(stderr, "Error: empty file '%s'\n", path);
        return NULL;
    }

    uint8_t* data = (uint8_t*)malloc((size_t)len);
    if (!data) {
        fclose(f);
        fprintf(stderr, "Error: malloc failed for %ld bytes\n", len);
        return NULL;
    }

    size_t read = fread(data, 1, (size_t)len, f);
    fclose(f);

    if (read != (size_t)len) {
        free(data);
        fprintf(stderr, "Error: short read on '%s'\n", path);
        return NULL;
    }

    *out_len = (size_t)len;
    return data;
}

// ---------------------------------------------------------------------------
// Audio event callback example
// ---------------------------------------------------------------------------

static void on_rive_event(const char* name, void* user_data) {
    (void)user_data;
    printf("[Rive Event] %s\n", name);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

#define WINDOW_WIDTH  640
#define WINDOW_HEIGHT 480

int main(int argc, char* argv[]) {
    const char* riv_path = (argc > 1) ? argv[1] : "test.riv";

    // Load .riv file
    size_t riv_len = 0;
    uint8_t* riv_data = load_file(riv_path, &riv_len);
    if (!riv_data) {
        fprintf(stderr, "Usage: %s <path/to/file.riv>\n", argv[0]);
        return 1;
    }

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        free(riv_data);
        return 1;
    }

    SDL_Window* win = SDL_CreateWindow("rive-thorvg",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        free(riv_data);
        return 1;
    }

    SDL_Renderer* sdl_renderer = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_renderer) {
        sdl_renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }

    SDL_Texture* texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        WINDOW_WIDTH, WINDOW_HEIGHT);

    // Pixel buffer for Rive rendering
    static uint32_t buffer[WINDOW_WIDTH * WINDOW_HEIGHT];

    // Initialize rive-thorvg
    rive_thorvg_init(2);  // 2 ThorVG worker threads for desktop

    rive_instance_t* rive = rive_create(buffer, WINDOW_WIDTH, WINDOW_HEIGHT,
                                        RIVE_PIXEL_FORMAT_ARGB8888);
    if (!rive) {
        fprintf(stderr, "rive_create failed\n");
        goto cleanup;
    }

    rive_set_fit(rive, RIVE_FIT_CONTAIN, RIVE_ALIGN_CENTER);
    rive_set_event_callback(rive, on_rive_event, NULL);

    // Load with optional artboard name from command line
    const char* artboard_name = (argc > 2) ? argv[2] : NULL;
    if (!rive_load_ex(rive, riv_data, riv_len, artboard_name, NULL)) {
        fprintf(stderr, "rive_load failed for '%s'\n", riv_path);
        goto cleanup;
    }

    printf("Loaded: %s (artboard: %.0f x %.0f)\n",
           riv_path,
           rive_artboard_width(rive),
           rive_artboard_height(rive));

    // Print state machine inputs
    int input_count = rive_input_count(rive);
    if (input_count > 0) {
        printf("State machine inputs (%d):\n", input_count);
        for (int i = 0; i < input_count; i++) {
            printf("  [%d] %s\n", i, rive_input_name(rive, i));
        }
    }

    // Main loop
    bool running = true;
    uint32_t last_tick = SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_MOUSEBUTTONDOWN: {
                    float ax, ay;
                    rive_screen_to_artboard(rive,
                        (float)ev.button.x, (float)ev.button.y, &ax, &ay);
                    rive_pointer_down(rive, ax, ay);
                    break;
                }

                case SDL_MOUSEMOTION: {
                    float ax, ay;
                    rive_screen_to_artboard(rive,
                        (float)ev.motion.x, (float)ev.motion.y, &ax, &ay);
                    rive_pointer_move(rive, ax, ay);
                    break;
                }

                case SDL_MOUSEBUTTONUP: {
                    float ax, ay;
                    rive_screen_to_artboard(rive,
                        (float)ev.button.x, (float)ev.button.y, &ax, &ay);
                    rive_pointer_up(rive, ax, ay);
                    break;
                }

                case SDL_KEYDOWN:
                    if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                    }
                    // Space: fire any trigger inputs
                    else if (ev.key.keysym.sym == SDLK_SPACE) {
                        int count = rive_input_count(rive);
                        for (int i = 0; i < count; i++) {
                            const char* name = rive_input_name(rive, i);
                            if (name) {
                                printf("Firing trigger: %s\n", name);
                                rive_fire_trigger(rive, name);
                            }
                        }
                    }
                    // B: toggle all boolean inputs
                    else if (ev.key.keysym.sym == SDLK_b) {
                        static bool toggle = false;
                        toggle = !toggle;
                        int count = rive_input_count(rive);
                        for (int i = 0; i < count; i++) {
                            const char* name = rive_input_name(rive, i);
                            if (name) {
                                printf("Setting bool '%s' = %s\n",
                                       name, toggle ? "true" : "false");
                                rive_set_bool(rive, name, toggle);
                            }
                        }
                    }
                    // N: fire ViewModel "Next" trigger
                    else if (ev.key.keysym.sym == SDLK_n) {
                        printf("VM trigger: Next\n");
                        rive_vm_fire_trigger(rive, "Next");
                    }
                    // P: fire ViewModel "Back" trigger
                    else if (ev.key.keysym.sym == SDLK_p) {
                        printf("VM trigger: Back\n");
                        rive_vm_fire_trigger(rive, "Back");
                    }
                    break;
            }
        }

        // Calculate elapsed time
        uint32_t now = SDL_GetTicks();
        float elapsed = (float)(now - last_tick) / 1000.0f;
        last_tick = now;

        // Clamp to prevent large time jumps
        if (elapsed > 0.1f) elapsed = 0.1f;

        // Advance and render
        rive_advance(rive, elapsed);

        // Update the SDL texture from the pixel buffer and present
        SDL_UpdateTexture(texture, NULL, buffer, WINDOW_WIDTH * 4);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderCopy(sdl_renderer, texture, NULL, NULL);
        SDL_RenderPresent(sdl_renderer);
    }

cleanup:
    if (rive) rive_destroy(rive);
    rive_thorvg_term();

    if (texture) SDL_DestroyTexture(texture);
    if (sdl_renderer) SDL_DestroyRenderer(sdl_renderer);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();

    free(riv_data);
    return 0;
}
