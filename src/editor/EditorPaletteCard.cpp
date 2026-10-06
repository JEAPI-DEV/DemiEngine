#include "editor/EditorPaletteCard.h"
#include <algorithm>

namespace demi::editor {

bool drawEditorPaletteCard(const EditorPaletteCard &card,
                           const char *payloadType,
                           std::span<const std::byte> payload) {
  ImGui::PushID(card.id.c_str());
  const float padding = 12.0F;
  const float width = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
  const float textWidth = std::max(width - padding * 2.0F, 1.0F);
  const float titleWidth = std::max(textWidth - 28.0F, 1.0F);
  const ImVec2 title =
      ImGui::CalcTextSize(card.title.c_str(), nullptr, false, titleWidth);
  const ImVec2 description =
      ImGui::CalcTextSize(card.description.c_str(), nullptr, false, textWidth);
  const float height =
      padding * 2.0F + std::max(title.y, 20.0F) + 8.0F + description.y;
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0F);
  ImGui::PushStyleColor(ImGuiCol_Button,
                        ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
  const bool activated = ImGui::Button("##palette-card", {width, height});
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  const ImVec2 topLeft = ImGui::GetItemRectMin();
  const ImVec2 bottomRight = ImGui::GetItemRectMax();
  auto &draw = *ImGui::GetWindowDrawList();
  const ImU32 accent = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
  draw.AddRect(topLeft, bottomRight,
               ImGui::IsItemHovered() ? accent
                                      : ImGui::GetColorU32(ImGuiCol_Border),
               6.0F);
  draw.AddLine({topLeft.x + 2.0F, topLeft.y + 8.0F},
               {topLeft.x + 2.0F, bottomRight.y - 8.0F}, accent, 3.0F);
  drawEditorGlyph(draw, card.icon,
                  {topLeft.x + padding + 9.0F, topLeft.y + padding + 10.0F},
                  ImGui::GetColorU32(ImGuiCol_Text));
  draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(),
               {topLeft.x + padding + 28.0F, topLeft.y + padding},
               ImGui::GetColorU32(ImGuiCol_Text), card.title.c_str(), nullptr,
               titleWidth);
  draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(),
               {topLeft.x + padding,
                topLeft.y + padding + std::max(title.y, 20.0F) + 8.0F},
               ImGui::GetColorU32(ImGuiCol_TextDisabled),
               card.description.c_str(), nullptr, textWidth);
  if (payloadType && !payload.empty() && ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(payloadType, payload.data(), payload.size());
    ImGui::TextUnformatted(card.title.c_str());
    ImGui::EndDragDropSource();
  }
  ImGui::Spacing();
  ImGui::PopID();
  return activated;
}

} // namespace demi::editor
