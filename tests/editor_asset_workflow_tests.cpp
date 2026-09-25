#include "editor/EditorWorkspace.h"

#include "demi/schema/Validation.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

void write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
}

void writeBytes(const std::filesystem::path &path,
                const std::vector<unsigned char> &bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

nlohmann::json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  return nlohmann::json::parse(input);
}

} // namespace

int main() {
  namespace fs = std::filesystem;
  const fs::path root =
      fs::temp_directory_path() / "demi_editor_asset_workflow_tests";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  write(
      root / "project/demi.project.json",
      R"({"format_version":1,"name":"Asset Editor","main_scene":"scene://main","scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}],"assets":[]})");
  write(root / "project/scenes/main.scene.json",
        R"({"format_version":1,"id":"scene://main","entities":[]})");
  write(root / "external/logo.png", "png-fixture");
  std::vector<unsigned char> geometry(72U);
  const float positions[]{-1.0F, -1.0F, -1.0F, 1.0F, -1.0F, -1.0F,
                          0.0F,  1.0F, -1.0F, 0.0F, 0.0F,  1.0F};
  const std::uint16_t indices[]{0, 1, 2, 0, 1, 3,
                                1, 2, 3, 2, 0, 3};
  std::memcpy(geometry.data(), positions, sizeof(positions));
  std::memcpy(geometry.data() + sizeof(positions), indices, sizeof(indices));
  writeBytes(root / "external/geometry.bin", geometry);
  write(root / "external/model.gltf", R"({
    "asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
    "nodes":[{"mesh":0}],
    "buffers":[{"uri":"geometry.bin","byteLength":72}],
    "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},
                   {"buffer":0,"byteOffset":48,"byteLength":24}],
    "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
    "accessors":[
      {"bufferView":0,"componentType":5126,"count":4,"type":"VEC3",
       "min":[-1,-1,-1],"max":[1,1,1]},
      {"bufferView":1,"componentType":5123,"count":12,"type":"SCALAR"}]
  })");

  demi::editor::EditorWorkspace workspace;
  std::string error;
  assert(workspace.open(root / "project", error));
  const auto sourceCount = workspace.sources().size();
  assert(workspace.createFolder({}, "assets", error));
  assert(workspace.createFolder("assets", "Props", error));
  assert(workspace.createFolder("assets/Props", "Empty folder", error));
  assert(workspace.sourceDirectories().contains("assets/Props/Empty folder"));
  assert(fs::is_empty(root / "project/assets/Props/Empty folder"));
  assert(workspace.sources().size() == sourceCount);
  assert(!workspace.hasUnsavedChanges());
  assert(!workspace.createFolder("assets", "Props", error));
  for (const std::string name :
       {"", ".", "..", "../escape", "nested/child", "back\\slash", "   ",
        ".git", ".demi", "build", "generated"})
    assert(!workspace.createFolder("assets", name, error));
  assert(!workspace.createFolder("..", "escape", error));
  assert(!workspace.createFolder(root / "external", "escape", error));
  assert(!workspace.createFolder("assets/missing", "child", error));
  assert(!workspace.createFolder("assets", std::string("bad\0name", 8), error));
  fs::create_directory_symlink(root / "external",
                               root / "project/assets/linked");
  assert(!workspace.createFolder("assets/linked", "escape", error));
  assert(!fs::exists(root / "external/escape"));
  write(root / "project/assets/file.txt", "fixture");
  assert(!workspace.createFolder("assets", "file.txt", error));
  assert(!workspace.createFolder("assets/file.txt", "child", error));
  fs::create_directories(root / "project/generated/hidden");
  assert(!workspace.createFolder("generated", "child", error));
  fs::create_directories(root / "project/external_empty/nested");
  workspace.refreshAssetMetadata();
  assert(workspace.sourceDirectories().contains("external_empty/nested"));
  assert(!workspace.sourceDirectories().contains("generated"));
  assert(!workspace.sourceDirectories().contains("assets/linked"));
  assert(workspace.createEntity(error));
  const auto dirtyScene = workspace.sceneDocument().json();
  assert(workspace.createFolder("assets", "While editing", error));
  assert(workspace.sceneDocument().json() == dirtyScene);
  assert(workspace.sceneDocument().isDirty());
  assert(workspace.undo(error));
  demi::editor::EditorWorkspace reopened;
  assert(reopened.open(root / "project", error));
  assert(reopened.sourceDirectories().contains("assets/Props/Empty folder"));
  assert(workspace.importAsset(
      {.source = root / "external/logo.png", .id = "asset://ui/logo"}, error));
  assert(workspace.assetIndex().assets().size() == 1);
  const auto manifest = workspace.assetIndex().assets().front().manifest;
  assert(manifest.id == "asset://ui/logo");
  assert(workspace.importAsset({.source = root / "external/model.gltf",
                                .id = "asset://models/triangle"},
                               error));
  const auto *model = demi::findAsset(workspace.assetIndex().registry(),
                                      "asset://models/triangle");
  assert(model != nullptr);
  const auto modelManifest = model->manifestPath;
  const auto recommendation =
      workspace.recommendCollider(modelManifest, "static", error);
  assert(recommendation && recommendation->shape == "triangle_mesh");
  std::optional<std::string> replacementHash;
  const auto collider = workspace.generateColliderAsset(
      {.modelManifestPath = modelManifest,
       .id = "asset://colliders/triangle",
       .detail = 1.0F,
       .body = "static"},
      replacementHash, error);
  assert(collider && !replacementHash);
  assert(demi::findAsset(workspace.assetIndex().registry(),
                         "asset://colliders/triangle") != nullptr);
  assert(!workspace.generateColliderAsset(
      {.modelManifestPath = modelManifest,
       .id = "asset://colliders/triangle",
       .detail = 0.0F,
       .body = "static"},
      replacementHash, error));
  assert(replacementHash);
  write(*collider, readJson(*collider).dump(2) + "\n\n");
  assert(!workspace.generateColliderAsset(
      {.modelManifestPath = modelManifest,
       .id = "asset://colliders/triangle",
       .detail = 0.0F,
       .body = "static",
       .replaceExisting = true,
       .expectedExistingManifestHash = replacementHash},
      replacementHash, error));
  assert(error.find("changed") != std::string::npos);
  replacementHash.reset();
  assert(!workspace.generateColliderAsset(
      {.modelManifestPath = modelManifest,
       .id = "asset://colliders/triangle",
       .detail = 0.0F,
       .body = "static"},
      replacementHash, error));
  assert(replacementHash);
  const auto replaced = workspace.generateColliderAsset(
      {.modelManifestPath = modelManifest,
       .id = "asset://colliders/triangle",
       .detail = 0.0F,
       .body = "static",
       .replaceExisting = true,
       .expectedExistingManifestHash = replacementHash},
      replacementHash, error);
  assert(replaced == collider);
  assert(workspace.createEntity(error));
  const std::string colliderEntity(workspace.selectedEntityId());
  assert(workspace.addComponent(colliderEntity, "AudioListener", error));
  assert(workspace.undo(error));
  const nlohmann::json beforeRejectedCollider =
      workspace.sceneDocument().json();
  const bool couldUndoBeforeRejectedCollider =
      workspace.sceneDocument().canUndo();
  const bool couldRedoBeforeRejectedCollider =
      workspace.sceneDocument().canRedo();
  const bool invalidColliderAssigned = workspace.addComponent(
      colliderEntity, "ModelCollider3D",
      {{"asset", "asset://colliders/missing"}}, error);
  if (invalidColliderAssigned)
    std::cerr << "Invalid Inspector collider assignment was accepted.\n";
  else if (error.empty())
    std::cerr << "Invalid Inspector collider assignment had no diagnostic.\n";
  assert(!invalidColliderAssigned);
  assert(!error.empty());
  assert(workspace.sceneDocument().json() == beforeRejectedCollider);
  assert(workspace.sceneDocument().canUndo() ==
         couldUndoBeforeRejectedCollider);
  assert(workspace.sceneDocument().canRedo() ==
         couldRedoBeforeRejectedCollider);
  assert(workspace.redo(error));
  assert(workspace.sceneDocument().component(colliderEntity,
                                             "AudioListener") != nullptr);
  assert(workspace.undo(error));
  const bool colliderAssigned = workspace.addComponent(
      colliderEntity, "ModelCollider3D",
      {{"asset", "asset://colliders/triangle"}}, error);
  if (!colliderAssigned)
    std::cerr << "Inspector collider assignment failed: " << error << '\n';
  assert(colliderAssigned);
  assert(workspace.sceneDocument()
             .component(colliderEntity, "ModelCollider3D")
             ->at("asset") == "asset://colliders/triangle");
  assert(workspace.undo(error));
  assert(workspace.sceneDocument().component(colliderEntity,
                                             "ModelCollider3D") == nullptr);
  assert(workspace.redo(error));
  assert(workspace.sceneDocument()
             .component(colliderEntity, "ModelCollider3D")
             ->at("asset") == "asset://colliders/triangle");
  assert(workspace.save(error));
  demi::editor::EditorWorkspace reopenedWithCollider;
  assert(reopenedWithCollider.open(root / "project", error));
  assert(demi::findAsset(reopenedWithCollider.assetIndex().registry(),
                         "asset://colliders/triangle") != nullptr);
  assert(reopenedWithCollider.sceneDocument()
             .component(colliderEntity, "ModelCollider3D")
             ->at("asset") == "asset://colliders/triangle");
  assert(workspace.createAssetGroup("asset-group://startup",
                                    {"asset://ui/logo"}, error));
  assert(workspace.assetIndex().groups().size() == 1);
  assert(workspace.setPreloadedAssets({"asset-group://startup"}, error));
  assert(workspace.projectDocument().isDirty());
  assert(workspace.projectUndo(error));
  assert(workspace.projectDocument().preloadedAssets().empty());
  assert(workspace.projectRedo(error));
  assert(workspace.saveProject(error));
  assert(!workspace.projectDocument().isDirty());
  assert(!demi::hasErrors(demi::validatePath(root / "project").diagnostics));

  write(manifest.sourcePath, "updated-png-fixture");
  workspace.refreshAssetMetadata();
  const auto *changedImage =
      workspace.assetIndex().findByManifest(manifest.manifestPath);
  assert(changedImage != nullptr && !changedImage->diagnostics.empty());
  assert(workspace.reimportAsset(manifest.manifestPath, error));
  assert(!demi::hasErrors(demi::validatePath(root / "project").diagnostics));

  fs::remove_all(root, ignored);
}
