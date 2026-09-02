#include "editor/EditorSpecializedDocument.h"

#include "demi/assets/AssetHash.h"
#include "demi/schema/Validation.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

void write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
}

void writeJson(const std::filesystem::path &path,
               const nlohmann::json &document) {
  write(path, document.dump(2) + '\n');
}

nlohmann::json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  return nlohmann::json::parse(input);
}

} // namespace

int main() {
  namespace fs = std::filesystem;
  using demi::editor::EditorSpecializedDocument;
  using demi::editor::EditorSpecializedKind;
  const fs::path root =
      fs::temp_directory_path() / "demi_editor_specialized_document_tests";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  writeJson(root / "demi.project.json",
            {{"format_version", 1},
             {"name", "Specialized"},
             {"main_scene", "scene://main"},
             {"scenes",
              {{{"id", "scene://main"}, {"path", "scenes/main.scene.json"}}}},
             {"assets", nlohmann::json::array()}});
  writeJson(root / "scenes/main.scene.json",
            {{"format_version", 1},
             {"id", "scene://main"},
             {"entities", nlohmann::json::array()}});

  const fs::path child = root / "prefabs/child.prefab.json";
  const fs::path owner = root / "prefabs/owner.prefab.json";
  writeJson(child, {{"format_version", 1},
                    {"id", "prefab://child"},
                    {"entities",
                     {{{"id", "body"},
                       {"name", "Before"},
                       {"components", nlohmann::json::object()}}}}});
  writeJson(owner, {{"format_version", 1},
                    {"id", "prefab://owner"},
                    {"entities", nlohmann::json::array()},
                    {"instances",
                     {{{"id", "nested"},
                       {"prefab", "prefab://child"},
                       {"overrides", {{"body", {{"name", "After"}}}}}}}}});

  std::vector<fs::path> sources{child, owner};
  demi::editor::EditorAssetIndex index;
  index.refresh(root, sources);
  std::string error;
  EditorSpecializedDocument prefab;
  assert(prefab.open(owner, index, error));
  assert(prefab.kind() == EditorSpecializedKind::Prefab);
  assert(prefab.expandedPrefab()["entities"].size() == 1);
  assert(!prefab.prefabDiff().empty());
  assert(prefab.applyPrefabOverrides(0, error));
  assert(readJson(child)["entities"][0]["name"] == "After");
  assert(!readJson(owner)["instances"][0].contains("overrides"));

  auto childRollback = readJson(child);
  childRollback["entities"][0]["name"] = "Before rollback";
  writeJson(child, childRollback);
  nlohmann::json ownerRollback{
      {"format_version", 1},
      {"id", "prefab://owner"},
      {"entities", nlohmann::json::array()},
      {"instances",
       {{{"id", "nested"},
         {"prefab", "prefab://child"},
         {"overrides", {{"body", {{"name", "Must roll back"}}}}}}}}};
  writeJson(owner, ownerRollback);
  EditorSpecializedDocument conflictingPrefab;
  assert(conflictingPrefab.open(owner, index, error));
  ownerRollback["name"] = "External writer";
  writeJson(owner, ownerRollback);
  assert(!conflictingPrefab.applyPrefabOverrides(0, error));
  assert(readJson(child)["entities"][0]["name"] == "Before rollback");
  assert(readJson(owner)["name"] == "External writer");

  const fs::path missing = root / "prefabs/missing.prefab.json";
  writeJson(missing,
            {{"format_version", 1},
             {"id", "prefab://missing"},
             {"entities", nlohmann::json::array()},
             {"instances",
              {{{"id", "broken"}, {"prefab", "prefab://does_not_exist"}}}}});
  EditorSpecializedDocument brokenPrefab;
  assert(brokenPrefab.open(missing, index, error));
  assert(demi::hasErrors(brokenPrefab.document().diagnostics()));
  fs::remove(missing);

  const fs::path hudPath = root / "scenes/main.hud.json";
  writeJson(hudPath, {{"format_version", 1},
                      {"canvas_size", {320, 180}},
                      {"root",
                       {{"id", "root"},
                        {"type", "container"},
                        {"anchor_min", {0, 0}},
                        {"anchor_max", {1, 1}},
                        {"children",
                         {{{"id", "title"},
                           {"type", "label"},
                           {"text", "Hello"},
                           {"position", {8, 9}},
                           {"size", {100, 24}}}}}}}});
  EditorSpecializedDocument hud;
  assert(!hud.open(hudPath, index, error));
  assert(error.find("integrated HUD viewport") != std::string::npos);

  const fs::path materialSource = root / "assets/materials/test.material.json";
  const fs::path materialManifest = root / "assets/materials/test.asset.json";
  writeJson(materialSource,
            {{"format_version", 1},
             {"shader", "builtin://lit"},
             {"parameters", {{"base_color", {1, 1, 1, 1}}}},
             {"render_state", {{"blend", "opaque"}, {"cull", "back"}}}});
  writeJson(materialManifest,
            {{"format_version", 1},
             {"id", "asset://materials/test"},
             {"type", "Material"},
             {"source", "test.material.json"},
             {"importer", "material"},
             {"importer_version", 1},
             {"source_hash", *demi::assets::hashFiles({materialSource})},
             {"dependencies", nlohmann::json::array()},
             {"settings", nlohmann::json::object()}});

  const fs::path modelSource = root / "assets/models/test.glb";
  const fs::path modelManifest = root / "assets/models/test.asset.json";
  write(modelSource, "model");
  nlohmann::json modelDocument{
      {"format_version", 1},
      {"id", "asset://models/test"},
      {"type", "Model3D"},
      {"source", "test.glb"},
      {"importer", "gltf-model"},
      {"importer_version", 1},
      {"source_hash", *demi::assets::hashFiles({modelSource})},
      {"dependencies", nlohmann::json::array()},
      {"settings", nlohmann::json::object()}};
  modelDocument["settings"]["animations"]["clips"] =
      nlohmann::json::array({{{"name", "Idle"}, {"skeleton", "human"}}});
  writeJson(modelManifest, modelDocument);

  const fs::path dataSource = root / "assets/data/dialogue.json";
  const fs::path dataManifest = root / "assets/data/dialogue.asset.json";
  writeJson(dataSource, {{"format_version", 1}, {"lines", {"Hello", "World"}}});
  writeJson(dataManifest,
            {{"format_version", 1},
             {"id", "asset://data/dialogue"},
             {"type", "DataAsset"},
             {"source", "dialogue.json"},
             {"importer", "json_data"},
             {"importer_version", 1},
             {"source_hash", *demi::assets::hashFiles({dataSource})},
             {"dependencies", nlohmann::json::array()},
             {"settings", {{"content_type", "dialogue"}}}});

  const fs::path audioSource = root / "assets/audio/hit.wav";
  const fs::path audioManifest = root / "assets/audio/hit.asset.json";
  write(audioSource, "audio");
  writeJson(audioManifest,
            {{"format_version", 1},
             {"id", "asset://audio/hit"},
             {"type", "AudioClip"},
             {"source", "hit.wav"},
             {"importer", "audio"},
             {"importer_version", 1},
             {"source_hash", *demi::assets::hashFiles({audioSource})},
             {"dependencies", nlohmann::json::array()},
             {"settings", {{"streaming", false}}}});

  sources.insert(sources.end(),
                 {materialSource, materialManifest, modelSource, modelManifest,
                  dataSource, dataManifest, audioSource, audioManifest});
  index.refresh(root, sources);
  EditorSpecializedDocument material;
  assert(material.open(materialSource, index, error));
  assert(material.kind() == EditorSpecializedKind::Material);
  assert(!material.document().set("/render_state/blend", "invalid", error));
  assert(material.document().set("/render_state/blend", "alpha", error));
  assert(material.document().undo(error));

  EditorSpecializedDocument animation;
  assert(animation.open(modelManifest, index, error));
  assert(animation.kind() == EditorSpecializedKind::Animation);
  EditorSpecializedDocument data;
  assert(data.open(dataSource, index, error));
  assert(data.kind() == EditorSpecializedKind::Data);
  EditorSpecializedDocument audio;
  assert(audio.open(audioManifest, index, error));
  assert(audio.kind() == EditorSpecializedKind::Audio);

  assert(!demi::hasErrors(demi::validatePath(root).diagnostics));
  fs::remove_all(root, ignored);
}
