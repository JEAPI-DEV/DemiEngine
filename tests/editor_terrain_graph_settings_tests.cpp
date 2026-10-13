#include "demi/assets/AssetHash.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphRegistry.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "editor/EditorTerrainGraphSettings.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Json = nlohmann::json;
using demi::editor::EditorWorkspace;
namespace fs = std::filesystem;

void require(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void writeJson(const fs::path &path, const Json &value) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path);
  output << value.dump(2) << '\n';
  require(output.good(), "Could not write settings fixture: " + path.string());
}

struct TestProject {
  fs::path root;

  TestProject() {
    const std::string pattern =
        (fs::temp_directory_path() / "demi-terrain-settings-ui-XXXXXX")
            .string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char *created = ::mkdtemp(buffer.data());
    require(created != nullptr, "Could not create terrain settings fixture");
    root = created;
    auto recipe = demi::runtime::TerrainRecipe{};
    recipe.size = {4, 4};
    recipe.cellsX = recipe.cellsZ = recipe.chunkCells = 4;
    recipe.landforms.at("default").heightVariation = 0;
    recipe.graph = demi::runtime::defaultTerrainGraph();
    writeJson(root / "demi.project.json",
              {{"format_version", 1},
               {"name", "Terrain settings UI"},
               {"main_scene", "scene://terrain_settings/main"},
               {"scenes",
                {{{"id", "scene://terrain_settings/main"},
                  {"path", "scenes/main.scene.json"}}}}});
    writeJson(root / "scenes/main.scene.json",
              {{"format_version", 1},
               {"id", "scene://terrain_settings/main"},
               {"entities",
                {{{"id", "terrain"},
                  {"components",
                   {{"Transform3D", Json::object()},
                    {"Terrain3D", {{"recipe", recipe.toJson()}}}}}}}}});

    const auto renderSource = root / "assets/materials/stone.material.json";
    writeJson(renderSource, {{"format_version", 1},
                             {"shader", "builtin://lit"},
                             {"parameters", {{"base_color", {1, 1, 1, 1}}}}});
    writeJson(root / "assets/materials/stone.asset.json",
              {{"format_version", 1},
               {"id", "asset://materials/stone"},
               {"type", "Material"},
               {"source", "stone.material.json"},
               {"importer", "material"},
               {"importer_version", 1},
               {"source_hash", *demi::assets::hashFiles({renderSource})},
               {"dependencies", Json::array()},
               {"settings", Json::object()}});

    const auto typedSource = root / "assets/terrain_materials/stone.json";
    writeJson(typedSource, {{"format_version", 1}, {"name", "Terrain stone"}});
    writeJson(root / "assets/terrain_materials/stone.asset.json",
              {{"format_version", 1},
               {"id", "asset://terrain_materials/stone"},
               {"type", "DataAsset"},
               {"source", "stone.json"},
               {"importer", "json_data"},
               {"importer_version", 1},
               {"source_hash", *demi::assets::hashFiles({typedSource})},
               {"dependencies", Json::array()},
               {"settings", {{"content_type", "terrain_material"}}}});
  }

  ~TestProject() {
    std::error_code error;
    fs::remove_all(root, error);
    if (error)
      std::cerr << "Could not clean settings fixture: " << error.message()
                << '\n';
  }

  TestProject(const TestProject &) = delete;
  TestProject &operator=(const TestProject &) = delete;
};

void initializeImGui() {
  ImGui::SetAllocatorFunctions(
      [](std::size_t size, void *) { return std::malloc(size); },
      [](void *memory, void *) { std::free(memory); });
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.DisplaySize = {900, 1100};
  io.DeltaTime = 1.0F / 60.0F;
  io.Fonts->AddFontDefault();
  unsigned char *pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  require(pixels != nullptr, "Could not prepare settings test font");
}

void drawSettings(EditorWorkspace &workspace, const char *windowName,
                  const std::string_view nodeType = {}, const float width = 340,
                  std::string *reportedNotice = nullptr) {
  std::string notice;
  ImGui::NewFrame();
  ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
  ImGui::SetNextWindowSize({width, 1000}, ImGuiCond_Always);
  ImGui::Begin(windowName);
  if (nodeType.empty())
    demi::editor::drawTerrainGraphSettings(workspace, notice);
  else
    demi::editor::drawTerrainNodeSettings(workspace, nodeType, notice);
  ImGui::End();
  ImGui::Render();
  require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame == 0,
          "Terrain settings produced an ImGui scope/layout error");
  if (reportedNotice != nullptr && !notice.empty())
    *reportedNotice = notice;
  else if (reportedNotice == nullptr)
    require(notice.empty(), "Terrain settings reported: " + notice);
}

ImGuiID scopedId(const char *windowName,
                 const std::initializer_list<const char *> labels) {
  const auto *window = ImGui::FindWindowByName(windowName);
  require(window != nullptr, "Settings window was not submitted");
  ImGuiID id = window->ID;
  for (const char *label : labels)
    id = ImHashStr(label, 0, id);
  return id;
}

ImGuiID biomePropertyId(const char *windowName, const char *label) {
  // An open TreeNode pushes its own ID before submitting property controls.
  return scopedId(windowName, {"default", "Biome properties", label});
}

template <typename Draw> void pressKey(const ImGuiKey key, Draw &&draw) {
  auto &io = ImGui::GetIO();
  io.AddKeyEvent(key, true);
  draw();
  io.AddKeyEvent(key, false);
  draw();
  // Navigation applies the submitted focus result on the following frame.
  draw();
}

template <typename Draw>
void focusWidget(const ImGuiID id, Draw &&draw, const char *stage = nullptr) {
  draw();
  auto *context = ImGui::GetCurrentContext();
  if (context->OpenPopupStack.empty()) {
    ImGuiWindow *owner = nullptr;
    for (auto *window : context->Windows) {
      if (window->Active && !window->IsFallbackWindow &&
          !(window->Flags & ImGuiWindowFlags_ChildWindow)) {
        require(owner == nullptr,
                "Settings fixture has ambiguous active windows");
        owner = window;
      }
    }
    require(owner != nullptr, "Settings fixture has no active window");
    // Focus through empty content padding. Repeated title-bar clicks within
    // ImGui's double-click interval legitimately collapse the fixture window,
    // making an otherwise reachable field disappear from navigation.
    const auto padding = ImGui::GetStyle().WindowPadding;
    require(padding.x > 0 && padding.y > 0,
            "Settings fixture needs an empty padding region for mouse focus");
    const ImVec2 focusPoint{owner->InnerRect.Max.x - padding.x * 0.5F,
                            owner->InnerRect.Min.y + padding.y * 0.5F};
    auto &io = ImGui::GetIO();
    io.AddMousePosEvent(focusPoint.x, focusPoint.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    draw();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    draw();
    draw();
  }
  // Tab uses ImGui's actual focus routing and opens scalar text entry without
  // forcing NavWindow, ActiveId or activation flags. This is a bounded fixture
  // timeout, not an editor/terrain content limit.
  for (int step = 0; step < 128 && context->NavId != id; ++step)
    pressKey(ImGuiKey_Tab, draw);
  require(context->NavId == id,
          "Keyboard navigation could not reach settings widget " +
              std::to_string(id) +
              (stage ? " during " + std::string(stage) : std::string{}) +
              " (focused " + std::to_string(context->NavId) + ", window " +
              (context->NavWindow ? context->NavWindow->Name : "none") + ")");
  draw();
}

template <typename Draw>
void activateWidget(const ImGuiID id, Draw &&draw,
                    const char *stage = nullptr) {
  focusWidget(id, draw, stage);
  pressKey(ImGuiKey_Space, draw);
}

template <typename Draw>
void replaceText(const ImGuiID id, const char *text, Draw &&draw) {
  focusWidget(id, draw);
  // Enter commits scalar text and leaves navigation on the widget while
  // clearing its active editor. Reactivate it before a second edit.
  if (ImGui::GetCurrentContext()->ActiveId != id)
    pressKey(ImGuiKey_Enter, draw);
  require(ImGui::GetCurrentContext()->ActiveId == id,
          std::string("Could not focus terrain settings input for '") + text +
              "' (requested " + std::to_string(id) + ", active " +
              std::to_string(ImGui::GetCurrentContext()->ActiveId) + ")");
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

void checkAppearanceControls(EditorWorkspace &workspace) {
  auto &draft = workspace.terrainAuthoring().draft();
  const auto source = workspace.sceneDocument().json();
  draft["biomes"]["default"] = Json::object();
  const auto omitted = draft;
  drawSettings(workspace, "Native defaults", "biomes", 290);
  drawSettings(workspace, "Native defaults", "biomes", 290);
  require(draft == omitted, "Viewing a default color authored its fallback");
  const auto native = demi::runtime::TerrainRecipe::parse(draft);
  const auto fallback = demi::runtime::TerrainBiome{}.color;
  const auto shown = native.biomes.at("default").color;
  require(shown.r == fallback.r && shown.g == fallback.g &&
              shown.b == fallback.b && shown.a == fallback.a,
          "Omitted color did not use the native biome default");

  draft["biomes"]["default"]["color"] = {0.1234567, 0.2345678, 0.3456789,
                                         0.8765432};
  const auto precise = draft;
  for (const char *type : {"landform", "biomes", "scatter", "output"}) {
    require(demi::runtime::terrainGraphNodeDefinition(type) != nullptr,
            "Context settings use a nonexistent native registry type");
    const auto name = std::string("Settings ") + type;
    drawSettings(workspace, name.c_str(), type, 290);
  }
  drawSettings(workspace, "Root settings", {}, 290);
  require(draft == precise, "Rendering settings quantized native color values");

  const auto draw = [&] {
    drawSettings(workspace, "Biome controls", "biomes");
  };
  draw();
  draw();
  // Editing the shared standard picker is covered by editor_color_control_tests.
  // This consumer check ensures opening settings preserves authored precision.
  require(draft == precise, "Opening biome color settings changed the source");
  const auto shared = draft;
  drawSettings(workspace, "Output controls", "output");
  drawSettings(workspace, "Root settings");
  require(
      draft == shared && workspace.sceneDocument().json() == source,
      "Context settings use a second model or changed source before Generate");
}

void checkRenderMaterialAndTextureScale(EditorWorkspace &workspace) {
  const auto &registry = workspace.assetIndex().registry();
  const auto *renderAsset =
      demi::findAsset(registry, "asset://materials/stone");
  const auto *typedAsset =
      demi::findAsset(registry, "asset://terrain_materials/stone");
  require(renderAsset && renderAsset->type == "Material" && typedAsset &&
              typedAsset->type == "DataAsset",
          "Fixture must register ordinary and typed material assets");
  auto &draft = workspace.terrainAuthoring().draft();
  const auto source = workspace.sceneDocument().json();
  const auto before = draft;
  auto biome = [&]() -> Json & { return draft["biomes"]["default"]; };
  biome().erase("material");
  biome().erase("texture_scale");
  const auto omitted = draft;
  const auto draw = [&] {
    drawSettings(workspace, "Biome surface controls", "biomes");
  };
  draw();
  draw();
  require(draft == omitted,
          "Viewing the render material or texture scale authored defaults");

  activateWidget(
      scopedId("Biome surface controls", {"default", "Biome properties"}),
      draw);
  activateWidget(
      biomePropertyId("Biome surface controls", "##Render material (Material)"),
      draw, "material selection");
  auto &popups = ImGui::GetCurrentContext()->OpenPopupStack;
  require(!popups.empty() && popups.back().Window != nullptr,
          "Biome properties did not expose the render material picker");
  activateWidget(popups.back().Window->GetID("asset://materials/stone"), draw);
  require(biome().at("material") == "asset://materials/stone",
          "Ordinary Material manifest could not be selected for terrain");
  require(!biome().contains("texture_scale"),
          "Choosing a material authored the texture scale fallback");

  activateWidget(
      biomePropertyId("Biome surface controls", "##Render material (Material)"),
      draw, "material clear after selection");
  require(!popups.empty() && popups.back().Window != nullptr,
          "Biome material picker did not reopen");
  activateWidget(popups.back().Window->GetID("None"), draw);
  require(draft == omitted,
          "Clearing the render material changed unrelated draft fields");

  const auto scaleField = biomePropertyId(
      "Biome surface controls", "##Texture scale (repeats/local unit, Enter)");
  replaceText(scaleField, "2048", draw);
  require(biome().at("texture_scale") == 2048.0F,
          "Texture scale rejected a valid value above common UI ranges");
  replaceText(scaleField, "0.125", draw);
  require(biome().at("texture_scale") == 0.125F,
          "Texture scale edit did not preserve a fractional local-unit value");
  require(demi::runtime::TerrainRecipe::parse(draft)
                  .biomes.at("default")
                  .textureScale == 0.125F,
          "The scale control did not produce a native biome value");
  const auto selected = draft;
  std::string notice;
  const auto drawInvalid = [&] {
    drawSettings(workspace, "Biome surface controls", "biomes", 340, &notice);
  };
  replaceText(scaleField, "0", drawInvalid);
  require(notice == "Texture scale must be a positive finite number." &&
              draft == selected,
          "Invalid texture scale changed the draft or lacked feedback");

  replaceText(scaleField, "1", draw);
  require(!biome().contains("texture_scale"),
          "Entering the canonical scale should restore omission");
  replaceText(scaleField, "0.25", draw);

  activateWidget(biomePropertyId("Biome surface controls", "Reset scale"), draw,
                 "texture scale reset after 0.25 edit");
  require(!biome().contains("texture_scale"),
          "Reset scale did not restore the omitted native default");
  require(demi::runtime::TerrainRecipe::parse(draft)
                  .biomes.at("default")
                  .textureScale == demi::runtime::TerrainBiome{}.textureScale,
          "Omitted texture scale did not use the native default");

  require(draft == omitted && workspace.sceneDocument().json() == source,
          "Reset or clearing material changed unrelated draft/source fields");
  require(before == omitted,
          "Fixture unexpectedly started with material or texture scale");
}

void checkSectionsAndRules(EditorWorkspace &workspace) {
  auto &draft = workspace.terrainAuthoring().draft();
  const auto before = draft;
  const auto drawRoot = [&] {
    drawSettings(workspace, "Collapsible settings");
  };
  drawRoot();
  drawRoot();
  const auto appearance =
      scopedId("Collapsible settings", {"Appearance - biome surfaces"});
  activateWidget(appearance, drawRoot);
  require(!ImGui::FindWindowByName("Collapsible settings")
               ->StateStorage.GetBool(appearance),
          "Recipe Appearance section did not collapse");
  require(draft == before, "Collapsing a section changed the terrain recipe");

  const auto drawBiomes = [&] {
    drawSettings(workspace, "Biome rules controls", "biomes");
  };
  drawBiomes();
  drawBiomes();
  const auto previousRules = draft.value("rules", Json::array()).size();
  activateWidget(scopedId("Biome rules controls", {"Add rule"}), drawBiomes);
  require(draft.at("rules").size() == previousRules + 1,
          "The Biome Rules context did not expose its actual add-rule control");
  (void)demi::runtime::TerrainRecipe::parse(draft);
}

void checkLandformReferences(EditorWorkspace &workspace) {
  auto &draft = workspace.terrainAuthoring().draft();
  draft["biomes"]["default"]["landform"] = "default";
  const auto source = workspace.sceneDocument().json();
  const auto draw = [&] {
    drawSettings(workspace, "Landform controls", "landform");
  };
  draw();
  draw();
  const auto tree = scopedId("Landform controls", {"default", "default"});
  activateWidget(tree, draw);
  const auto rename = scopedId("Landform controls",
                               {"default", "default", "##Landform ID (Enter)"});
  replaceText(rename, "shared_shape", draw);
  require(
      draft.at("landforms").contains("shared_shape") &&
          !draft.at("landforms").contains("default") &&
          draft.at("default_landform") == "shared_shape" &&
          draft.at("biomes").at("default").at("landform") == "shared_shape",
      "Landform context rename did not update shared biome/default references");
  require(workspace.sceneDocument().json() == source,
          "Landform context edited source instead of the shared draft");
  (void)demi::runtime::TerrainRecipe::parse(draft);
}

void checkPlacementControls(EditorWorkspace &workspace) {
  auto &draft = workspace.terrainAuthoring().draft();
  draft["palette"] = "asset://terrain/scatter";
  const auto draw = [&] {
    drawSettings(workspace, "Scatter controls", "scatter");
  };
  draw();
  draw();
  activateWidget(scopedId("Scatter controls", {"##Scatter palette asset"}),
                 draw);
  auto &popups = ImGui::GetCurrentContext()->OpenPopupStack;
  require(!popups.empty() && popups.back().Window != nullptr,
          "The Scatter context did not expose its actual palette picker");
  activateWidget(popups.back().Window->GetID("None"), draw);
  require(!draft.contains("palette"),
          "Clearing scatter authored an invalid empty reference");
  const auto before = draft;
  drawSettings(workspace, "Unsupported context", "biome_rules");
  require(draft == before, "Unsupported node context changed the draft");
  (void)demi::runtime::TerrainRecipe::parse(draft);
}

} // namespace

int main() {
  try {
    TestProject fixture;
    EditorWorkspace workspace;
    std::string error;
    require(workspace.open(fixture.root, error), error);
    workspace.selectEntity("terrain");
    workspace.syncTerrainAuthoring();
    initializeImGui();
    checkAppearanceControls(workspace);
    checkRenderMaterialAndTextureScale(workspace);
    checkSectionsAndRules(workspace);
    checkLandformReferences(workspace);
    checkPlacementControls(workspace);
    ImGui::DestroyContext();
    std::cout << "Terrain graph settings controls passed\n";
    return 0;
  } catch (const std::exception &error) {
    if (ImGui::GetCurrentContext() != nullptr)
      ImGui::DestroyContext();
    std::cerr << error.what() << '\n';
    return 1;
  }
}
