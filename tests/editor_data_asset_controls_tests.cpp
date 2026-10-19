#include "editor/EditorDataAssetControls.h"
#include "editor/EditorJsonDocument.h"
#include "editor/EditorSourceCreation.h"
#include "editor/EditorSpecializedDocument.h"
#include "editor/EditorSpecializedPanel.h"
#include "editor/EditorWorkspace.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataDocument.h"
#include "demi/assets/MaterialSet.h"
#include "demi/assets/MaterialSurface.h"
#include "demi/assets/RenderAsset.h"
#include "demi/runtime/terrain/TerrainPalette.h"
#include "editor/EditorMaterialControls.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void writeJson(const std::filesystem::path &path, const nlohmann::json &value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << value.dump(2) << '\n';
  require(output.good(), "Could not write " + path.string());
}

nlohmann::json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  require(input.good(), "Could not read " + path.string());
  return nlohmann::json::parse(input);
}

class ImGuiFixture {
public:
  ImGuiFixture() {
    ImGui::SetAllocatorFunctions(
        [](std::size_t size, void *) { return std::malloc(size); },
        [](void *memory, void *) { std::free(memory); });
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {900, 1100};
    io.DeltaTime = 1.F / 60.F;
    io.Fonts->AddFontDefault();
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    require(pixels != nullptr, "Could not prepare the ImGui font atlas");
    auto *context = ImGui::GetCurrentContext();
    context->ErrorCallback = [](ImGuiContext *, void *userData,
                                const char *message) {
      static_cast<std::vector<std::string> *>(userData)->emplace_back(message);
    };
    context->ErrorCallbackUserData = &warnings_;
  }

  ~ImGuiFixture() { ImGui::DestroyContext(); }

  void draw(demi::editor::EditorWorkspace &workspace,
            demi::editor::EditorJsonDocument &document,
            std::string_view contentType, std::string &notice) {
    ImGui::NewFrame();
    ImGui::Begin("Data controls");
    if (contentType == "material") {
      demi::editor::drawEditorMaterialSurfaceControls(workspace, document,
                                                      notice);
    } else {
      require(demi::editor::drawEditorDataAssetControls(workspace, document,
                                                        contentType, notice),
              "Expected native controls for " + std::string(contentType));
    }
    ImGui::End();
    ImGui::Render();
    const auto *context = ImGui::GetCurrentContext();
    require(warnings_.empty() && context->ErrorCountCurrentFrame == 0,
            "Data controls produced an ImGui warning: " +
                (warnings_.empty() ? std::string{} : warnings_.front()));
  }

private:
  std::vector<std::string> warnings_;
};

demi::editor::EditorJsonValidator
nativeValidator(const demi::AssetRegistry &registry, std::string_view kind) {
  return [registry, kind = std::string(kind)](
             const std::filesystem::path &path,
             const nlohmann::json &value) -> demi::Diagnostics {
    auto parsed = demi::assets::parseDataDocument(value.dump(), path);
    if (!parsed.document)
      return parsed.diagnostics;
    try {
      if (kind == "material") {
        demi::Diagnostics diagnostics;
        (void)demi::assets::parseMaterialAsset(value, path, &diagnostics);
        return diagnostics;
      } else if (kind == "terrain_material_set") {
        (void)demi::assets::parseTerrainMaterialSet(*parsed.document, registry,
                                                    "asset://test");
      } else
        (void)demi::runtime::parseTerrainPalette(*parsed.document, registry,
                                                 "asset://test");
    } catch (const std::invalid_argument &failure) {
      return {{.severity = demi::Severity::Error,
               .code = "DATA_INVALID",
               .message = failure.what(),
               .path = path.string()}};
    }
    return {};
  };
}

void open(demi::editor::EditorJsonDocument &document,
          const std::filesystem::path &path,
          const demi::AssetRegistry &registry, std::string_view kind) {
  std::string error;
  require(document.open(path, nativeValidator(registry, kind), error), error);
  require(!demi::hasErrors(document.diagnostics()),
          "Fixture must pass the native parser");
}

} // namespace

int main() {
  namespace fs = std::filesystem;
  const fs::path project = fs::path(DEMI_SOURCE_DIR) / "examples/terrain_3d";
  const demi::AssetRegistry registry = demi::loadAssetRegistry(project);
  demi::editor::EditorWorkspace workspace;
  ImGuiFixture imgui;
  std::string notice;

  demi::editor::EditorJsonDocument materialSet;
  open(materialSet, project / "assets/materials/terrain_pbr.json", registry,
       "terrain_material_set");
  const nlohmann::json originalSet = materialSet.json();
  imgui.draw(workspace, materialSet, "terrain_material_set", notice);
  auto *window = ImGui::FindWindowByName("Data controls");
  require(window != nullptr, "Material-set controls window is missing");
  const std::string documentId = materialSet.path().generic_string();
  const ImGuiID scope = ImHashStr(documentId.c_str(), 0, window->ID);
  ImGui::ActivateItemByID(ImHashStr("ground", 0, scope));
  imgui.draw(workspace, materialSet, "terrain_material_set", notice);
  require(materialSet.canUndo(),
          "Required-role toggle must use document history");
  require(materialSet.json() != originalSet,
          "Required-role toggle must change the authored document");
  std::string error;
  require(materialSet.undo(error), error);
  require(materialSet.json() == originalSet,
          "Undo must restore the original material set");
  require(materialSet.redo(error), error);
  require(materialSet.json() != originalSet,
          "Redo must restore the required-role edit");

  nlohmann::json singleRoleSet = originalSet;
  singleRoleSet["roles"] = {{"ground", originalSet.at("roles").at("ground")}};
  singleRoleSet["required_roles"] = nlohmann::json::array();
  require(materialSet.replace(singleRoleSet, error), error);
  imgui.draw(workspace, materialSet, "terrain_material_set", notice);
  const ImGuiID roleScope = ImHashStr("ground", 0, scope);
  ImGui::ActivateItemByID(ImHashStr("Remove", 0, roleScope));
  imgui.draw(workspace, materialSet, "terrain_material_set", notice);
  require(materialSet.json() == singleRoleSet,
          "Last material role must not be removed");

  demi::editor::EditorJsonDocument palette;
  open(palette, project / "assets/terrain/palettes/meadow.json", registry,
       "terrain_palette");
  imgui.draw(workspace, palette, "terrain_palette", notice);
  nlohmann::json singleRolePalette = palette.json();
  singleRolePalette["placements"] = {
      {"tree", palette.json().at("placements").at("tree")}};
  singleRolePalette.erase("material_set");
  require(palette.replace(singleRolePalette, error), error);
  imgui.draw(workspace, palette, "terrain_palette", notice);

  std::string scratchTemplate =
      (fs::temp_directory_path() / "demi_data_controls_XXXXXX").string();
  char *createdDirectory = ::mkdtemp(scratchTemplate.data());
  require(createdDirectory != nullptr,
          "Could not create temporary test folder");
  const fs::path scratch(createdDirectory);
  const fs::path materialPath = scratch / "sample.json";
  {
    std::ofstream output(materialPath);
    output << R"({"format_version":1,"name":"Sample"})";
    require(output.good(), "Could not write Material fixture");
  }
  demi::editor::EditorJsonDocument material;
  open(material, materialPath, registry, "material");
  imgui.draw(workspace, material, "material", notice);
  require(demi::editor::setEditorMaterialMap(
              material, demi::assets::MaterialMapSlot::DetailNormal,
              "asset://textures/detail", error),
          error);
  require(material.set("/parameters/detail_strength", .5, error), error);
  const auto withDetail = material.json();
  require(demi::editor::setEditorMaterialMap(
              material, demi::assets::MaterialMapSlot::DetailNormal, "", error),
          error);
  require(!material.json().at("textures").contains("detail_normal") &&
              material.json().at("parameters").at("detail_strength") == 0,
          "Clearing detail map did not reset its dependent strength");
  require(material.undo(error) && material.json() == withDetail, error);
  require(material.redo(error), error);
  const auto originalMaterial = material.json();
  require(material.set("/mapping", "triplanar", error), error);
  require(material.set("/normal_map_convention", "directx", error), error);
  require(material.set("/parameters/normal_strength", 2.0, error), error);
  require(material.set("/parameters/tiling", 3.5, error), error);
  const auto editedMaterial = material.json();
  require(!material.set("/mapping", "planar", error) &&
              material.json() == editedMaterial,
          "Invalid mapping changed the material document");
  require(!material.set("/parameters/tiling", 0, error) &&
              material.json() == editedMaterial,
          "Invalid tiling changed the material document");
  require(material.undo(error) && material.redo(error), error);
  require(material.json() == editedMaterial,
          "Material Undo/Redo did not restore surface controls");
  imgui.draw(workspace, material, "material", notice);
  require(material.json() == editedMaterial,
          "Viewing Material controls authored unrelated defaults");
  require(material.save(error), error);
  require(readJson(materialPath) == editedMaterial,
          "Material save lost the unified surface fields");
  require(originalMaterial.at("format_version") ==
              material.json().at("format_version"),
          "Surface editing changed document version");

  ImGui::NewFrame();
  ImGui::Begin("Data controls");
  require(!demi::editor::drawEditorDataAssetControls(workspace, material,
                                                     "unrecognized", notice),
          "Unknown content type should be left to the generic inspector");
  ImGui::End();
  ImGui::Render();

  const fs::path authoredProject = scratch / "project";
  writeJson(authoredProject / "demi.project.json",
            {{"format_version", 1},
             {"name", "Data controls"},
             {"main_scene", "scene://main"},
             {"scenes",
              nlohmann::json::array({{{"id", "scene://main"},
                                      {"path", "scenes/main.scene.json"}}})}});
  writeJson(authoredProject / "scenes/main.scene.json",
            {{"format_version", 1},
             {"id", "scene://main"},
             {"entities", nlohmann::json::array()}});

  demi::editor::EditorWorkspace authoredWorkspace;
  require(authoredWorkspace.open(authoredProject, error), error);
  fs::path created;
  require(demi::editor::createEditorSource(
              authoredWorkspace, demi::editor::EditorSourceKind::Material,
              "original", created, error),
          error);
  require(demi::editor::createEditorSource(
              authoredWorkspace, demi::editor::EditorSourceKind::Material,
              "new_material", created, error),
          error);
  const std::string originalRef = "asset://materials/original";
  const std::string newRef = "asset://materials/new_material";
  require(demi::editor::createEditorSource(
              authoredWorkspace,
              demi::editor::EditorSourceKind::TerrainMaterialSet, "surface",
              created, error, {}, {}, {.initialAsset = originalRef}),
          error);
  const fs::path setManifest = created;
  const fs::path setSource =
      authoredProject / "assets/terrain_material_sets/surface/surface.json";
  const nlohmann::json originalManifest = readJson(setManifest);
  const nlohmann::json originalSource = readJson(setSource);
  require(std::find(originalManifest.at("dependencies").begin(),
                    originalManifest.at("dependencies").end(),
                    newRef) == originalManifest.at("dependencies").end(),
          "Fixture already declares the new reference");

  demi::editor::EditorSpecializedDocument specialized;
  require(specialized.open(setManifest, authoredWorkspace.assetIndex(), error),
          error);
  require(specialized.dataContentType() == "terrain_material_set",
          "Created set did not open with typed controls");
  auto &authoredDocument = specialized.document();
  require(authoredDocument.set("/roles/rock", newRef, error), error);
  require(authoredDocument.json().at("roles").at("rock") == newRef,
          "Specialized validator rejected the new material reference");
  require(readJson(setSource) == originalSource &&
              readJson(setManifest) == originalManifest,
          "Unsaved role edit changed source or manifest on disk");
  imgui.draw(authoredWorkspace, authoredDocument, specialized.dataContentType(),
             notice);
  require(authoredDocument.undo(error), error);
  require(authoredDocument.json() == originalSource,
          "Undo did not restore the created material set");
  require(authoredDocument.redo(error), error);
  require(authoredDocument.json().at("roles").at("rock") == newRef,
          "Redo did not restore the new material reference");
  require(authoredDocument.save(error), error);
  require(readJson(setManifest) == originalManifest,
          "Saving source changed the manifest before reimport");
  require(authoredWorkspace.reimportAsset(setManifest, error), error);
  const nlohmann::json updatedManifest = readJson(setManifest);
  require(std::find(updatedManifest.at("dependencies").begin(),
                    updatedManifest.at("dependencies").end(),
                    newRef) != updatedManifest.at("dependencies").end(),
          "Reimport did not register the new material dependency");

  demi::editor::EditorSpecializedPanel panel;
  auto edited = readJson(setSource);
  edited["name"] = "Retryable saved source";
  require(panel.restore(
              {.path = setSource, .kind = "specialized", .content = edited},
              authoredWorkspace, error),
          error);
  auto brokenManifest = updatedManifest;
  brokenManifest["source"] = "missing-source-for-reimport.json";
  writeJson(setManifest, brokenManifest);
  require(
      !panel.saveActive(authoredWorkspace, error) && panel.isDirty() &&
          error.find("Source saved") != std::string::npos &&
          readJson(setSource) == edited,
      "Failed reimport lost the retry state or misreported the saved source");
  const auto pending = panel.recoveryDocument();
  require(pending.has_value(),
          "Pending asset import was omitted from recovery");
  require(!panel.open(authoredProject /
                          "assets/materials/original/original.material.json",
                      authoredWorkspace.assetIndex(), error),
          "Opening another source discarded a pending import");
  writeJson(setManifest, updatedManifest);
  require(panel.saveActive(authoredWorkspace, error) && !panel.isDirty(),
          error);
  demi::editor::EditorSpecializedPanel recovered;
  require(recovered.restore(*pending, authoredWorkspace, error) &&
              recovered.isDirty(),
          error);
  require(recovered.saveActive(authoredWorkspace, error) &&
              !recovered.isDirty(),
          error);

  fs::remove_all(scratch);
  return 0;
}
