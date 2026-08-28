#include "editor/EditorAboutPanel.h"

#include "demi/core/Version.h"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <imgui.h>

#include <algorithm>

namespace demi::editor {

void EditorAboutPanel::draw(const std::uint16_t logoTextureIndex,
                            std::string &notice) {
  if (!show_)
    return;

  ImGui::SetNextWindowSize({570.0F, 410.0F}, ImGuiCond_Appearing);
  if (!ImGui::Begin("About", &show_,
                    ImGuiWindowFlags_NoSavedSettings |
                        ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  if (logoTextureIndex != UINT16_MAX) {
    constexpr float LogoWidth = 477.0F;
    constexpr float LogoHeight = LogoWidth * 248.0F / 530.0F;
    ImGui::SetCursorPosX(
        std::max((ImGui::GetContentRegionAvail().x - LogoWidth) * 0.5F, 0.0F));
    ImGui::Image(bgfx::TextureHandle{logoTextureIndex},
                 {LogoWidth, LogoHeight});
  } else {
    ImGui::Dummy({0.0F, 36.0F});
    const char *name = EngineName.data();
    const float width = ImGui::CalcTextSize(name).x;
    ImGui::SetCursorPosX(
        std::max((ImGui::GetContentRegionAvail().x - width) * 0.5F, 0.0F));
    ImGui::TextUnformatted(name);
    ImGui::Dummy({0.0F, 36.0F});
  }

  ImGui::Separator();
  ImGui::Text("Version %s", EngineBuildVersion.data());
  ImGui::TextDisabled("Engine %s · build iteration %s", EngineVersion.data(),
                      EngineBuildIteration.data());
  ImGui::Spacing();
  if (ImGui::Button(EngineRepositoryUrl.data(), {-1.0F, 30.0F})) {
    if (!SDL_OpenURL(EngineRepositoryUrl.data()))
      notice = std::string("Could not open repository: ") + SDL_GetError();
    else
      notice = "Opened the DemiEngine repository";
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Open the DemiEngine GitHub repository");

  ImGui::End();
}

} // namespace demi::editor
