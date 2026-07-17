// SPDX-License-Identifier: MIT
#include "ui/main_window.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <cstdio>

#include "core/machine.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

namespace ab {

int MainWindow::run(int headlessFrames) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_Window* window = SDL_CreateWindow(
        "AppleBox", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 130");

    bool running = true;
    int frames = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        // Phase 0: the scheduler advances an empty timeline each frame to
        // prove the master clock runs; machines drive it from Phase 1 on.
        scheduler_.runUntil(scheduler_.now() + 1'000'000);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        drawUi();
        ImGui::Render();

        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);

        if (headlessFrames > 0 && ++frames >= headlessFrames) running = false;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

void MainWindow::drawUi() {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                 ImGuiDockNodeFlags_PassthruCentralNode);

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Machine")) {
            if (ImGui::MenuItem("Configure...")) configDialog_.open();
            ImGui::Separator();
            ImGui::MenuItem("Demo window", nullptr, &showDemo_);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (configDialog_.draw(machineConfig_)) {
        // Machine instantiation begins in Phase 1.
    }

    ImGui::Begin("Status");
    ImGui::Text("AppleBox 0.1.0 — Phase 0 skeleton");
    ImGui::Text("Master clock: %lld ticks", static_cast<long long>(scheduler_.now()));
    ImGui::Text("Machines in catalog: %zu (0 implemented)", machineCatalog().size());
    ImGui::TextDisabled("Place ROMs under roms/<machine>/ to enable models.");
    ImGui::End();

    if (showDemo_) ImGui::ShowDemoWindow(&showDemo_);
}

} // namespace ab
