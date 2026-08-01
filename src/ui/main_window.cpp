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

    openAudio();

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
        else if (apple2_)
            apple2_->run(Apple2PlusMachine::kClockHz / 60);
        else
            scheduler_.runUntil(scheduler_.now() + 1'000'000);

        pumpAudio();

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
    closeAudio();
    if (apple2Tex_) glDeleteTextures(1, &apple2Tex_);
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
        if (ImGui::BeginMenu("Audio")) {
            ImGui::MenuItem("Mute", nullptr, &muted_);
            ImGui::SetNextItemWidth(140.0f);
            ImGui::SliderFloat("Volume", &volume_, 0.0f, 1.0f, "%.2f");
            if (audioDevice_ == 0)
                ImGui::TextDisabled("(no audio device)");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (configDialog_.draw(machineConfig_)) startMachine();

    ImGui::Begin("Status");
    ImGui::Text("AppleBox 0.1.0 — Phase 2");
    if (apple1_) {
        ImGui::Text("Machine: Apple I @ 1.023 MHz");
        ImGui::Text("Master clock: %lld ticks",
                    static_cast<long long>(apple1_->scheduler().now()));
    } else if (apple2_) {
        ImGui::Text("Machine: Apple II+ @ 1.02 MHz");
        ImGui::Text("Master clock: %lld ticks",
                    static_cast<long long>(apple2_->scheduler().now()));
        ImGui::Text("Speaker: %llu toggles%s",
                    static_cast<unsigned long long>(apple2_->speakerToggles()),
                    muted_ ? " (muted)" : "");
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
    if (apple2_) {
        drawApple2Screen();
        if (apple2_->disk2().hasBootRom()) drawDiskUi();
    }

    if (showDemo_) ImGui::ShowDemoWindow(&showDemo_);
}

void MainWindow::startMachine() {
    machineError_.clear();
    apple1_.reset();
    apple2_.reset();
    terminal_.clear();
    terminalCol_ = 0;
    if (machineConfig_.machineId == "apple1") {
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
    } else if (machineConfig_.machineId == "apple2plus") {
        auto m = std::make_unique<Apple2PlusMachine>();
        if (!m->loadRoms("roms")) {
            machineError_ =
                "apple2plus: roms/apple2plus/apple2plus.rom missing or invalid";
            return;
        }
        m->reset();
        apple2_ = std::move(m);
    } else if (!machineConfig_.machineId.empty()) {
        machineError_ = machineConfig_.machineId + ": not yet implemented";
    }
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

void MainWindow::drawApple2Screen() {
    // Render the current frame from machine RAM + softswitch state.
    Apple2VideoState st;
    st.text = apple2_->textMode();
    st.mixed = apple2_->mixedMode();
    st.page2 = apple2_->page2();
    st.hires = apple2_->hires();
    // Flash characters alternate at ~2 Hz on real hardware.
    st.flash = std::fmod(ImGui::GetTime(), 0.5) < 0.25;
    apple2Video_.render(apple2_->ram(), st);

    if (apple2Tex_ == 0) {
        glGenTextures(1, &apple2Tex_);
        glBindTexture(GL_TEXTURE_2D, apple2Tex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, apple2Tex_);
    // Pixel-store state is shared and is modified by ImGui's font-atlas
    // uploads and by SDL; set it explicitly or a stale GL_UNPACK_ROW_LENGTH
    // makes glTexImage2D read past the end of the framebuffer.
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Apple2Video::kWidth,
                 Apple2Video::kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 apple2Video_.framebuffer().data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // 2x vertical scale corrects the Apple II's non-square pixel aspect.
    const ImVec2 size(Apple2Video::kWidth * 2.0f, Apple2Video::kHeight * 2.0f);
    ImGui::SetNextWindowSize(ImVec2(size.x + 16, size.y + 36),
                             ImGuiCond_FirstUseEver);
    ImGui::Begin("Apple II Screen");
    ImGui::Image(static_cast<ImTextureID>(apple2Tex_), size);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        ImGuiIO& io = ImGui::GetIO();
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            unsigned c = io.InputQueueCharacters[i];
            if (c >= 0x20 && c < 0x7f)
                apple2_->typeChar(static_cast<char>(c));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
            apple2_->typeChar('\r');
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) apple2_->typeChar(0x1b);
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
            apple2_->typeChar(0x08); // left arrow
    }
    ImGui::End();
}

void MainWindow::drawDiskUi() {
    auto& card = apple2_->disk2();
    ImGui::SetNextWindowSize(ImVec2(460, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("Disk II (slot 6)");

    ImGui::Text("Motor: %s   Head: track %.1f   Active: drive %d",
                card.motorOn() ? "on" : "off", card.headTrack(),
                card.selectedDrive() + 1);
    ImGui::Separator();

    ImGui::InputText("Image path", diskPath_.data(), diskPath_.size());
    ImGui::TextDisabled("Accepts .dsk .do .po .2mg .nib .woz");

    for (int i = 0; i < Disk2Controller::kDrives; ++i) {
        ImGui::PushID(i);
        const auto& d = card.drive(i);
        ImGui::Text("Drive %d: %s", i + 1,
                    d.disk ? d.disk->name().c_str() : "(empty)");
        if (d.disk) {
            ImGui::SameLine();
            ImGui::TextDisabled("[%s%s]", d.disk->format().c_str(),
                                d.disk->writeProtected() ? ", write protected"
                                                         : "");
        }
        if (ImGui::Button("Insert")) {
            diskError_.clear();
            std::string err;
            if (!card.insertDisk(i, diskPath_.data(), &err))
                diskError_ = err;
        }
        ImGui::SameLine();
        if (ImGui::Button("Eject")) card.ejectDisk(i);
        ImGui::SameLine();
        if (ImGui::Button("Insert + Reboot")) {
            diskError_.clear();
            std::string err;
            if (card.insertDisk(i, diskPath_.data(), &err))
                apple2_->reset();
            else
                diskError_ = err;
        }
        ImGui::PopID();
    }

    if (!diskError_.empty())
        ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s",
                           diskError_.c_str());
    ImGui::End();
}

void MainWindow::openAudio() {
    SDL_AudioSpec want{};
    want.freq = Speaker::kDefaultSampleRate;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = nullptr; // queued audio; no callback thread to lock against
    SDL_AudioSpec got{};
    audioDevice_ = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
    if (audioDevice_ == 0) {
        std::fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(audioDevice_, 0);
}

void MainWindow::closeAudio() {
    if (audioDevice_) {
        SDL_CloseAudioDevice(audioDevice_);
        audioDevice_ = 0;
    }
}

void MainWindow::pumpAudio() {
    if (!audioDevice_ || !apple2_) return;
    Speaker& spk = apple2_->speaker();
    spk.setVolume(volume_);
    spk.setMuted(muted_);

    // Keep at most ~100 ms queued: more is latency, less risks underruns.
    const Uint32 maxQueued =
        sizeof(float) * static_cast<Uint32>(spk.sampleRate() / 10);
    if (SDL_GetQueuedAudioSize(audioDevice_) > maxQueued) {
        // The machine is outrunning real time; drop what we just rendered
        // rather than letting the delay grow without bound.
        spk.clear();
        return;
    }

    float buffer[4096];
    while (std::size_t n = spk.read(buffer, std::size(buffer))) {
        SDL_QueueAudio(audioDevice_, buffer,
                       static_cast<Uint32>(n * sizeof(float)));
        if (n < std::size(buffer)) break;
    }
}

} // namespace ab
