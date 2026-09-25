#include "editor/EditorStructuredValue.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
} // namespace

int main() {
  try {
    ImGui::SetAllocatorFunctions(
        [](std::size_t size, void *) { return std::malloc(size); },
        [](void *memory, void *) { std::free(memory); });
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1000, 1200};
    io.DeltaTime = 1.0F / 60;
    io.Fonts->AddFontDefault();
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    nlohmann::json value = nlohmann::json::object();
    demi::editor::StructuredValueState state;
    bool readOnly = false;
    const auto frame = [&] {
      ImGui::NewFrame();
      ImGui::SetNextWindowSize({400, 1100}, ImGuiCond_Always);
      ImGui::Begin("Data");
      const auto edit =
          demi::editor::drawStructuredValue(value, state, 0, readOnly);
      ImGui::End();
      ImGui::Render();
      return edit;
    };
    frame();
    frame();
    auto *window = ImGui::FindWindowByName("Data");
    const ImGuiID collection = ImHashStr("##collection", 0, window->ID);
    const auto activate = [&](ImGuiID id) {
      ImGui::ActivateItemByID(id);
      return frame();
    };
    const auto popupAction = [&](const char *label) {
      const auto &stack = ImGui::GetCurrentContext()->OpenPopupStack;
      require(!stack.empty() && stack.back().Window,
              "Expected an editing popup");
      return activate(stack.back().Window->GetID(label));
    };
    const auto addField = [&] {
      activate(ImHashStr("Add field...", 0, collection));
      frame();
    };
    addField();
    io.AddInputCharactersUTF8("health");
    frame();
    require(state.key == "health", "Name input did not retain typed draft");
    state.type = 4;
    auto edit = popupAction("Add");
    require(edit.changed && edit.finished && !edit.continuous,
            "Add is not a discrete edit");
    require(value.at("health") == 0, "Typed integer field was not added");

    addField();
    state.key = "health";
    popupAction("Add");
    require(!state.error.empty() && value.size() == 1,
            "Duplicate name replaced data");
    popupAction("Cancel");
    require(value.size() == 1, "Cancel mutated data");

    const ImGuiID health = ImHashStr("health", 0, collection);
    activate(ImHashStr("Edit", 0, health));
    frame();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddInputCharactersUTF8("active");
    frame();
    state.type = 5;
    popupAction("Apply");
    require(value == nlohmann::json({{"active", false}}),
            "Rename/type editing failed");
    activate(ImHashStr("Remove", 0, ImHashStr("active", 0, collection)));
    require(value.empty(), "Removing field failed");

    value = nlohmann::json::array({"first", "second"});
    frame();
    activate(ImHashStr("Up", 0, ImHashStr("1", 0, collection)));
    require(value == nlohmann::json::array({"second", "first"}),
            "Array reorder failed");
    readOnly = true;
    activate(ImHashStr("Remove", 0, ImHashStr("0", 0, collection)));
    require(value.size() == 2, "Read-only value changed");
    readOnly = false;

    value = {{"text", ""}};
    frame();
    activate(ImHashStr("##value", 0, ImHashStr("text", 0, collection)));
    const std::string longText(5000, 'x');
    io.AddInputCharactersUTF8(longText.c_str());
    frame();
    require(value.at("text") == longText, "Text input was truncated");
    ImGui::DestroyContext();
    std::cout << "Structured-value interactions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
