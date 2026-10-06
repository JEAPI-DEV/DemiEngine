#include "editor/EditorSourceCreation.h"
#include "editor/EditorWorkspace.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/MaterialAsset.h"
#include "demi/assets/MaterialSet.h"
#include "demi/runtime/terrain/TerrainPalette.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct TemporaryProject {
  fs::path root;
  TemporaryProject() {
    const auto pattern =
        (fs::temp_directory_path() / "demi-editor-data-creation-XXXXXX")
            .string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    require(::mkdtemp(buffer.data()) != nullptr,
            "Could not make a temporary project");
    root = buffer.data();
  }
  ~TemporaryProject() {
    std::error_code error;
    fs::remove_all(root, error);
    if (error)
      std::cerr << "Could not remove test project: " << error.message() << '\n';
  }
};

void writeJson(const fs::path &path, const Json &value) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path);
  output << value.dump(2) << '\n';
  require(output.good(), "Could not write " + path.string());
}

Json readJson(const fs::path &path) {
  std::ifstream input(path);
  require(input.good(), "Could not read " + path.string());
  return Json::parse(input);
}

void createProject(const fs::path &root) {
  writeJson(root / "demi.project.json",
            {{"format_version", 1},
             {"name", "Data creation"},
             {"main_scene", "scene://main"},
             {"scenes", Json::array({{{"id", "scene://main"},
                                      {"path", "scenes/main.scene.json"}}})}});
  writeJson(root / "scenes/main.scene.json", {{"format_version", 1},
                                              {"id", "scene://main"},
                                              {"entities", Json::array()}});
}

void createModelFixture(const fs::path &root) {
  const auto folder = root / "assets/models/rock";
  fs::create_directories(folder);
  {
    std::ofstream mesh(folder / "rock.obj");
    mesh << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    require(mesh.good(), "Could not write model fixture");
  }
  const auto sourceHash = demi::assets::hashFiles({folder / "rock.obj"});
  require(sourceHash.has_value(), "Could not hash model fixture");
  writeJson(folder / "rock.asset.json", {{"format_version", 1},
                                         {"id", "asset://models/rock"},
                                         {"type", "Model3D"},
                                         {"source", "rock.obj"},
                                         {"importer", "obj-model"},
                                         {"importer_version", 1},
                                         {"source_hash", *sourceHash},
                                         {"dependencies", Json::array()},
                                         {"settings", Json::object()}});
}

void createPrefabFixture(const fs::path &root) {
  writeJson(
      root / "prefabs/rock.prefab.json",
      {{"format_version", 1},
       {"id", "prefab://rock"},
       {"entities",
        Json::array({{{"id", "body"},
                      {"components", {{"Transform3D", Json::object()}}}}})}});
}
} // namespace

int main() {
  using namespace demi;
  using namespace demi::editor;
  try {
    TemporaryProject project;
    createProject(project.root);
    EditorWorkspace workspace;
    std::string error;
    require(workspace.open(project.root, error), error);
    fs::path created;

    require(createEditorSource(workspace, EditorSourceKind::Material,
                               "general_paint", created, error),
            error);
    const auto sharedChoices = editorSourceAssetChoices(workspace);
    require(std::find(sharedChoices.materials.begin(),
                      sharedChoices.materials.end(),
                      "asset://materials/general_paint") !=
                sharedChoices.materials.end(),
            "Creation choices excluded an ordinary renderer material");
    require(
        createEditorSource(workspace, EditorSourceKind::TerrainMaterialSet,
                           "general_material_set", created, error, {}, {},
                           {.initialAsset = "asset://materials/general_paint"}),
        error);

    require(createEditorSource(workspace, EditorSourceKind::TerrainMaterial,
                               "earth/clay", created, error),
            error);
    const auto materialManifest = created;
    const auto materialSource =
        project.root / "assets/terrain_materials/earth/clay/clay.json";
    const auto material = readJson(materialSource);
    require(material == Json({{"format_version", 1},
                              {"name", "clay"},
                              {"base_color", {0.8, 0.8, 0.8, 1.0}}}),
            "Terrain material did not use the native starter format");
    require(readJson(materialManifest).at("settings").at("content_type") ==
                "terrain_material",
            "Terrain material importer lost content_type");
    auto registry = loadAssetRegistry(project.root);
    require(assets::loadTerrainMaterialAsset(
                registry, "asset://terrain_materials/earth/clay")
                .has_value(),
            "Created terrain material did not load");
    require(!createEditorSource(workspace, EditorSourceKind::TerrainMaterial,
                                "earth/clay", created, error) &&
                readJson(materialSource) == material,
            "Duplicate creation overwrote authored material");

    require(!createEditorSource(workspace, EditorSourceKind::TerrainMaterialSet,
                                "starter", created, error),
            "Material set was created without a reference");
    require(!fs::exists(project.root /
                        "assets/terrain_material_sets/starter/starter.json"),
            "Invalid material set wrote source");
    require(!createEditorSource(
                workspace, EditorSourceKind::TerrainMaterialSet, "starter",
                created, error, {}, {},
                {.initialAsset = "asset://terrain_materials/missing"}),
            "Material set accepted an unresolved reference");

    const EditorSourceAssetOptions setOptions{
        .initialAsset = "asset://terrain_materials/earth/clay"};
    require(createEditorSource(workspace, EditorSourceKind::TerrainMaterialSet,
                               "starter", created, error, {}, {}, setOptions),
            error);
    const auto setManifest = readJson(created);
    require(setManifest.at("settings").at("content_type") ==
                    "terrain_material_set" &&
                setManifest.at("dependencies") ==
                    Json::array({setOptions.initialAsset}),
            "Material set import did not register its content and dependency");
    require(readJson(project.root /
                     "assets/terrain_material_sets/starter/starter.json") ==
                Json({{"format_version", 1},
                      {"name", "starter"},
                      {"roles", {{"ground", setOptions.initialAsset}}}}),
            "Material set did not author one valid ground role");
    registry = loadAssetRegistry(project.root);
    require(assets::loadTerrainMaterialSet(
                registry, "asset://terrain_material_sets/starter")
                .has_value(),
            "Created material set did not load");

    require(!createEditorSource(workspace, EditorSourceKind::TerrainPalette,
                                "forest", created, error),
            "Palette was created without a reference");
    createModelFixture(project.root);
    workspace.refreshAssetMetadata();
    require(!createEditorSource(workspace, EditorSourceKind::TerrainMaterialSet,
                                "wrong_type", created, error, {}, {},
                                {.initialAsset = "asset://models/rock"}),
            "Material set accepted a Model3D as a material");
    const auto choices = editorSourceAssetChoices(workspace);
    require(choices.materials ==
                    std::vector<std::string>{"asset://materials/general_paint",
                                             setOptions.initialAsset} &&
                choices.models ==
                    std::vector<std::string>{"asset://models/rock"},
            "Dialog choices did not filter the native asset registry");
    const EditorSourceAssetOptions paletteOptions{.initialAsset =
                                                      "asset://models/rock"};
    require(createEditorSource(workspace, EditorSourceKind::TerrainPalette,
                               "forest", created, error, {}, {},
                               paletteOptions),
            error);
    const auto paletteManifest = readJson(created);
    require(paletteManifest.at("settings").at("content_type") ==
                    "terrain_palette" &&
                paletteManifest.at("dependencies") ==
                    Json::array({paletteOptions.initialAsset}),
            "Palette import did not register its content and dependency");
    require(
        readJson(project.root / "assets/terrain_palettes/forest/forest.json") ==
            Json({{"format_version", 1},
                  {"name", "forest"},
                  {"roles",
                   {{"soil", {{"asset", paletteOptions.initialAsset}}}}}}),
        "Palette did not author one valid soil role");
    registry = loadAssetRegistry(project.root);
    require(
        runtime::loadTerrainPalette(registry, "asset://terrain_palettes/forest")
            .has_value(),
        "Created palette did not load");
    require(!createEditorSource(workspace, EditorSourceKind::TerrainPalette,
                                "material_only", created, error, {}, {},
                                {.initialAsset = setOptions.initialAsset}),
            "Palette accepted a material without prefab geometry");
    require(
        !fs::exists(project.root /
                    "assets/terrain_palettes/material_only/material_only.json"),
        "Rejected palette wrote source");

    createPrefabFixture(project.root);
    workspace.refreshAssetMetadata();
    require(editorSourceAssetChoices(workspace).prefabs ==
                std::vector<std::string>{"prefab://rock"},
            "Palette choices did not discover the valid entity prefab");
    const EditorSourceAssetOptions prefabPaletteOptions{
        .initialAsset = setOptions.initialAsset,
        .initialPrefab = "prefab://rock"};
    require(createEditorSource(workspace, EditorSourceKind::TerrainPalette,
                               "prefab_surface", created, error, {}, {},
                               prefabPaletteOptions),
            error);
    const auto prefabPalette =
        readJson(project.root /
                 "assets/terrain_palettes/prefab_surface/prefab_surface.json");
    require(prefabPalette.at("roles").at("soil") ==
                Json({{"asset", setOptions.initialAsset},
                      {"prefab", "prefab://rock"}}),
            "Palette omitted the selected prefab or material");
    require(readJson(created).at("dependencies") ==
                Json::array({setOptions.initialAsset}),
            "Palette manifest includes a non-asset prefab dependency");
    return 0;
  } catch (const std::exception &failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
