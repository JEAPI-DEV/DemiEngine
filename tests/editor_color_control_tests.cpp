#include "editor/EditorColorControl.h"
#include "editor/EditorHudColorEncoding.h"
#include "editor/EditorInspectorModel.h"

#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/ui/UiDocumentParser.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <ranges>
#include <stdexcept>

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void initializeImGui() {
  ImGui::SetAllocatorFunctions(
      [](std::size_t size, void *) { return std::malloc(size); },
      [](void *memory, void *) { std::free(memory); });
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.DisplaySize = {600, 500};
  io.DeltaTime = 1.0F / 60.0F;
  io.Fonts->AddFontDefault();
  unsigned char *pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  require(pixels != nullptr, "Could not prepare color control font");
}

template <typename Draw> void pressKey(ImGuiKey key, Draw &&draw) {
  auto &io = ImGui::GetIO();
  io.AddKeyEvent(key, true);
  draw();
  io.AddKeyEvent(key, false);
  draw();
  draw();
}

template <typename Draw> void focusWidget(ImGuiID id, Draw &&draw) {
  draw();
  auto *context = ImGui::GetCurrentContext();
  if (context->OpenPopupStack.empty()) {
    auto *window = ImGui::FindWindowByName("Color control");
    require(window != nullptr, "Color control window was not submitted");
    const auto padding = ImGui::GetStyle().WindowPadding;
    auto &io = ImGui::GetIO();
    io.AddMousePosEvent(window->InnerRect.Max.x - padding.x * 0.5F,
                        window->InnerRect.Min.y + padding.y * 0.5F);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    draw();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    draw();
    draw();
  }
  for (int step = 0; step < 128 && context->NavId != id; ++step)
    pressKey(ImGuiKey_Tab, draw);
  require(context->NavId == id,
          "Keyboard navigation could not reach the color control");
  draw();
}

template <typename Draw>
void replaceText(ImGuiID id, const char *text, Draw &&draw) {
  focusWidget(id, draw);
  // Navigation focus alone does not activate DragFloat's text input.
  if (ImGui::GetCurrentContext()->ActiveId != id) {
    ImGui::ActivateItemByID(id);
    ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
    draw();
  }
  require(ImGui::GetCurrentContext()->ActiveId == id,
          "Could not activate precision channel");
  auto &io = ImGui::GetIO();
  io.AddKeyEvent(ImGuiMod_Ctrl, true);
  io.AddKeyEvent(ImGuiKey_A, true);
  draw();
  io.AddKeyEvent(ImGuiKey_A, false);
  io.AddKeyEvent(ImGuiMod_Ctrl, false);
  draw();
  io.AddInputCharactersUTF8(text);
  for (int frame = 0; frame < 3; ++frame)
    draw();
  pressKey(ImGuiKey_Enter, draw);
}

void checkPrecisionAndIdentity() {
  std::array<float, 4> rgba{0.1234567F, 0.2345678F, 0.3456789F, 0.8765432F};
  std::array<float, 4> other{0.25F, 0.5F, 0.75F, 1.0F};
  const auto original = rgba;
  const auto otherOriginal = other;
  const auto draw = [&] {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({400, 450}, ImGuiCond_Always);
    ImGui::Begin("Color control");
    const bool changed = demi::editor::drawEditorColorControl(
        "##color", rgba.data(),
        {.flags = ImGuiColorEditFlags_DisplayRGB |
                  ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float |
                  ImGuiColorEditFlags_AlphaBar});
    const bool otherChanged = demi::editor::drawEditorColorControl(
        "##other-color", other.data(), {.flags = ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Float});
    ImGui::End();
    ImGui::Render();
    require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame == 0,
            "Color control produced an ImGui scope error");
    return changed || otherChanged;
  };
  require(!draw() && !draw() && rgba == original && other == otherOriginal,
          "Rendering a color quantized or authored its channels");
  auto *window = ImGui::FindWindowByName("Color control");
  const ImGuiID colorScope = ImHashStr("##color", 0, window->ID);
  const ImGuiID otherScope = ImHashStr("##other-color", 0, window->ID);
  const ImGuiID red = ImHashStr("##X", 0, colorScope);
  const ImGuiID otherRed = ImHashStr("##X", 0, otherScope);
  require(red != otherRed, "Colour controls share an ImGui ID");
  replaceText(red, "0.654321", draw);
  require(std::abs(rgba[0] - 0.654321F) < 0.000001F && rgba[1] == original[1] &&
              rgba[2] == original[2] && rgba[3] == original[3],
          "Colour edit rounded or changed unrelated channels");
  replaceText(otherRed, "0.765432", draw);
  require(std::abs(other[0] - 0.765432F) < 0.000001F &&
              other[1] == otherOriginal[1] &&
              std::abs(rgba[0] - 0.654321F) < 0.000001F,
          "Second colour edit changed the first colour");
}

void checkConsumerEncoding() {
  namespace runtime = demi::runtime;
  const auto *mesh =
      runtime::scene_loading::findComponentDescriptor("MeshRenderer");
  require(mesh != nullptr, "MeshRenderer descriptor is missing");
  const auto field = std::ranges::find(
      mesh->fields, "color", &runtime::ComponentFieldDescriptor::name);
  require(field != mesh->fields.end(), "MeshRenderer color field is missing");
  const nlohmann::json wire = {{"color", {0.25, 0.5, 0.75, 1.0}}};
  const auto presentation = demi::editor::editorPropertyPresentation(
      *mesh, *field, wire, false, true);
  require(presentation.hasValue && presentation.value == wire.at("color"),
          "Inspector model changed the component color encoding");
  const auto parsed = runtime::scene_loading::colorField(wire, "color");
  require(parsed && parsed->r == 0.25F && parsed->g == 0.5F &&
              parsed->b == 0.75F && parsed->a == 1.0F,
          "MeshRenderer color parser did not read normalized float channels");
  nlohmann::json serialized;
  require(runtime::runtimeFieldJson(*parsed, serialized) &&
              serialized == wire.at("color"),
          "Native component color did not serialize as float RGBA");

  const auto opaque = demi::editor::editorHudColorHex({0.2F, 0.4F, 0.6F, 1.0F});
  const auto translucent =
      demi::editor::editorHudColorHex({0.2F, 0.4F, 0.6F, 128.0F / 255.0F});
  require(opaque == "#336699" && translucent == "#33669980",
          "HUD swatch conversion did not produce RGB/RGBA hex bytes");
  const auto hud = runtime::ui::parseUiDocument(
      {{"format_version", 1},
       {"root", {{"id", "root"}, {"type", "panel"}, {"color", translucent}}}});
  require(hud.nodes.size() == 1 &&
              std::abs(hud.nodes.front().color.a - 128.0F / 255.0F) <
                  0.000001F &&
              hud.nodes.front().color.r == 0.2F,
          "HUD hex swatch value did not decode to normalized native color");
}

} // namespace

int main() {
  try {
    initializeImGui();
    checkPrecisionAndIdentity();
    checkConsumerEncoding();
    ImGui::DestroyContext();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Editor color control checks passed\n";
  return 0;
}
