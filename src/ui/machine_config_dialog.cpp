// SPDX-License-Identifier: MIT
#include "ui/machine_config_dialog.h"

#include <vector>

#include "core/machine.h"
#include "imgui.h"

namespace ab {

namespace {

// Families in catalog/display order.
constexpr CpuFamily kFamilies[] = {
    CpuFamily::M6502,  CpuFamily::M65C816, CpuFamily::M68000,
    CpuFamily::M68020, CpuFamily::M68030,  CpuFamily::M68040,
    CpuFamily::PPC601, CpuFamily::PPC603_604, CpuFamily::PPC750,
};

std::vector<const MachineDesc*> modelsForFamily(CpuFamily f) {
    std::vector<const MachineDesc*> out;
    for (const auto& m : machineCatalog())
        if (m.family == f) out.push_back(&m);
    return out;
}

} // namespace

bool MachineConfigDialog::draw(MachineConfig& config) {
    if (openRequested_) {
        ImGui::OpenPopup("Machine Configuration");
        openRequested_ = false;
    }

    bool accepted = false;
    ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Machine Configuration", nullptr,
                                ImGuiWindowFlags_NoSavedSettings))
        return false;

    // --- Machine type (CPU family) dropdown ---
    if (ImGui::BeginCombo("Machine type", cpuFamilyName(kFamilies[familyIdx_]))) {
        for (int i = 0; i < static_cast<int>(std::size(kFamilies)); ++i) {
            bool selected = (i == familyIdx_);
            if (ImGui::Selectable(cpuFamilyName(kFamilies[static_cast<std::size_t>(i)]), selected)) {
                familyIdx_ = i;
                modelIdx_ = 0;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // --- Machine model dropdown (ROM-gated availability) ---
    auto models = modelsForFamily(kFamilies[familyIdx_]);
    if (modelIdx_ >= static_cast<int>(models.size())) modelIdx_ = 0;
    const char* current = models.empty() ? "(none)" : models[static_cast<std::size_t>(modelIdx_)]->name.c_str();
    if (ImGui::BeginCombo("Machine", current)) {
        for (int i = 0; i < static_cast<int>(models.size()); ++i) {
            const MachineDesc* m = models[static_cast<std::size_t>(i)];
            bool avail = machineAvailable(*m, romRoot_);
            ImGui::BeginDisabled(!avail);
            if (ImGui::Selectable(m->name.c_str(), i == modelIdx_)) modelIdx_ = i;
            ImGui::EndDisabled();
            if (!avail && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(m->phase <= 0 ? "Not yet implemented"
                                                : "Required ROMs not found in roms/%s/",
                                  m->romDir.c_str());
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();

    // --- 86Box-style tabs (contents populated per machine in later phases) ---
    if (ImGui::BeginTabBar("configTabs")) {
        for (const char* tab : {"Machine", "Video", "Storage", "Sound", "Ports", "Input"}) {
            if (ImGui::BeginTabItem(tab)) {
                ImGui::TextDisabled("Configuration for '%s' arrives with its machine phase.", tab);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    bool anyAvailable = !models.empty() &&
                        machineAvailable(*models[static_cast<std::size_t>(modelIdx_)], romRoot_);
    ImGui::BeginDisabled(!anyAvailable);
    if (ImGui::Button("OK", ImVec2(100, 0))) {
        config.machineId = models[static_cast<std::size_t>(modelIdx_)]->id;
        accepted = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
    return accepted;
}

} // namespace ab
