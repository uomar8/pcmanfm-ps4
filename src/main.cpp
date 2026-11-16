#include "app.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "log.h"
#include "ps4_ime.h"

#include <SDL2/SDL.h>
#include <orbis/SystemService.h>
#include <stdio.h>

static void sdl_log_bridge(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    (void)userdata;
    (void)category;
    (void)priority;
    log_msg("SDL2", "%s", message ? message : "");
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    log_init();
    log_msg("MAIN", "PCManFM-PS4 starting up...");

    SDL_LogSetOutputFunction(sdl_log_bridge, NULL);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
        log_msg("MAIN", "SDL_Init failed: %s", SDL_GetError());
        log_close();
        sceSystemServiceLoadExec("exit", NULL);
        return -1;
    }
    log_msg("MAIN", "SDL_Init OK");
    SDL_LogSetOutputFunction(sdl_log_bridge, NULL);

    SDL_Window* window = SDL_CreateWindow(
        "PCManFM-PS4",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1920,
        1080,
        SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN
    );
    if (!window) {
        log_msg("MAIN", "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        log_close();
        sceSystemServiceLoadExec("exit", NULL);
        return -1;
    }
    log_msg("MAIN", "SDL_CreateWindow(1920x1080) OK");

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer) {
        log_msg("MAIN", "Accelerated renderer failed (%s), trying software renderer", SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        log_msg("MAIN", "SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        log_close();
        sceSystemServiceLoadExec("exit", NULL);
        return -1;
    }
    SDL_RendererInfo rinfo;
    if (SDL_GetRendererInfo(renderer, &rinfo) == 0) {
        log_msg("MAIN", "SDL_CreateRenderer OK (driver=%s, renderer=%s)",
                SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "unknown",
                rinfo.name ? rinfo.name : "unknown");
    } else {
        log_msg("MAIN", "SDL_CreateRenderer OK");
    }

    // Hide PS4 splash screen now that video output is ready
    int32_t splash_rc = sceSystemServiceHideSplashScreen();
    log_msg("MAIN", "sceSystemServiceHideSplashScreen() -> 0x%08x", splash_rc);

    // Open DualShock 4 game controllers
    int num_joysticks = SDL_NumJoysticks();
    log_msg("MAIN", "SDL_NumJoysticks() = %d", num_joysticks);
    for (int i = 0; i < num_joysticks; ++i) {
        if (SDL_IsGameController(i)) {
            SDL_GameController* gc = SDL_GameControllerOpen(i);
            log_msg("MAIN", "Opened controller %d: %s", i, gc ? SDL_GameControllerName(gc) : "NULL");
        }
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = "/data/pcmanfm/imgui.ini";

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    PcmanfmApp app;
    app.init();

    log_msg("MAIN", "Entering main loop");
    bool running = true;
    while (running && !app.should_exit()) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                if (SDL_IsGameController(ev.cdevice.which)) {
                    SDL_GameController* gc = SDL_GameControllerOpen(ev.cdevice.which);
                    log_msg("MAIN", "Hotplug controller %d: %s", ev.cdevice.which, gc ? SDL_GameControllerName(gc) : "NULL");
                }
            }
            app.handle_sdl_event(ev);
            if (ev.type == SDL_QUIT) {
                running = false;
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        app.render_frame();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 22, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    log_msg("MAIN", "Exiting main loop cleanly");
    app.shutdown();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    log_close();
    sceSystemServiceLoadExec("exit", NULL);
    return 0;
}
