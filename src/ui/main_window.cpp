// SPDX-License-Identifier: MIT
#include "ui/main_window.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <cmath>
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

        // Advance emulated time ~one display frame per host frame.
        if (apple1_)
            apple1_->run(Apple1Machine::kClockHz / 60);
        else
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

    if (configDialog_.draw(machineConfig_)) startMachine();

    ImGui::Begin("Status");
    ImGui::Text("AppleBox 0.1.0 — Phase 1");
    if (apple1_) {
        ImGui::Text("Machine: Apple I @ 1.023 MHz");
        ImGui::Text("Master clock: %lld ticks",
                    static_cast<long long>(apple1_->scheduler().now()));
    } else {
        ImGui::Text("No machine running.");
        ImGui::TextDisabled("Machine > Configure... to select one.");
        ImGui::TextDisabled("Place ROMs under roms/<machine>/ to enable models.");
    }
    if (!machineError_.empty())
        ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s",
                           machineError_.c_str());
    ImGui::End();

    if (apple1_) drawTerminal();

    if (showDemo_) ImGui::ShowDemoWindow(&showDemo_);
}

void MainWindow::startMachine() {
    machineError_.clear();
    apple1_.reset();
    terminal_.clear();
    terminalCol_ = 0;
    if (machineConfig_.machineId != "apple1") {
        if (!machineConfig_.machineId.empty())
            machineError_ = machineConfig_.machineId + ": not yet implemented";
        return;
    }
    auto m = std::make_unique<Apple1Machine>();
    if (!m->loadRoms("roms")) {
        machineError_ = "apple1: roms/apple1/wozmon.rom missing or invalid";
        return;
    }
    m->onDisplayChar = [this](char c) {
        // 40-column display; CR is the only control character.
        if (c == '\r') {
            terminal_.push_back('\n');
            terminalCol_ = 0;
        } else if (c >= 0x20) {
            terminal_.push_back(c);
            if (++terminalCol_ >= 40) {
                terminal_.push_back('\n');
                terminalCol_ = 0;
            }
        }
    };
    m->reset();
    apple1_ = std::move(m);
}

void MainWindow::drawTerminal() {
    ImGui::SetNextWindowSize(ImVec2(480, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Apple I Terminal");

    ImGui::BeginChild("##term", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
    // The Apple-1 terminal hardware displays a blinking "@" at the cursor
    // position (it is not printed by the Woz Monitor).
    std::string display = terminal_;
    if (std::fmod(ImGui::GetTime(), 1.0) < 0.5) display.push_back('@');
    ImGui::TextUnformatted(display.c_str());
    ImGui::PopStyleColor();
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1)
        ImGui::SetScrollHereY(1.0f);

    // Keyboard: route typed characters to the machine while hovered/focused.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        ImGuiIO& io = ImGui::GetIO();
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            unsigned c = io.InputQueueCharacters[i];
            if (c >= 0x20 && c < 0x7f)
                apple1_->typeChar(static_cast<char>(c));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
            apple1_->typeChar('\r');
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) apple1_->typeChar(0x1b);
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace ab
