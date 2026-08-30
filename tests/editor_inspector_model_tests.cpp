#include "editor/EditorInspectorModel.h"

#include <cassert>

int main() {
  using namespace demi;
  using namespace demi::editor;
  const nlohmann::json scene = {
      {"entities",
       {{{"id", "a"},
         {"name", "Alpha"},
         {"components",
          {{"Transform3D",
            {{"position", {1.0, 2.0, 3.0}}, {"scale", {1.0, 1.0, 1.0}}}}}}},
        {{"id", "b"},
         {"name", "Beta"},
         {"components",
          {{"Transform3D",
            {{"position", {4.0, 5.0, 6.0}}, {"scale", {1.0, 1.0, 1.0}}}}}}}}}};

  const auto entities = editorReferenceChoices(
      runtime::ComponentReferenceKind::Entity, {}, {}, scene, {});
  assert(entities.size() == 2);
  assert(entities[0].id == "a");
  assert(entities[1].id == "b");

  const std::vector<std::string> selection{"a", "b"};
  const auto common = editorCommonFields(scene, selection);
  const auto position = std::ranges::find_if(common, [](const auto &field) {
    return field.field->name == "position";
  });
  const auto scale = std::ranges::find_if(
      common, [](const auto &field) { return field.field->name == "scale"; });
  assert(position != common.end() && position->mixed);
  assert(position->targets.size() == 2);
  assert(scale != common.end() && !scale->mixed);

  const nlohmann::json entity3D = {
      {"components", {{"Transform3D", nlohmann::json::object()}}}};
  const auto components = editorComponentChoices(entity3D);
  const auto transform2D =
      std::ranges::find_if(components, [](const auto &choice) {
        return choice.descriptor->name == "Transform2D";
      });
  assert(transform2D != components.end());
  assert(!transform2D->compatible);
  assert(!transform2D->incompatibility.empty());
  const auto buildable =
      std::ranges::find_if(components, [](const auto &choice) {
        return choice.descriptor->name == "Buildable";
      });
  assert(buildable != components.end() && buildable->compatible);
  const nlohmann::json alreadyBuildable = {
      {"components", {{"Buildable", nlohmann::json::object()}}}};
  assert(std::ranges::none_of(editorComponentChoices(alreadyBuildable),
                              [](const auto &choice) {
                                return choice.descriptor->name == "Buildable";
                              }));
  assert(editorComponentMatchesSearch("build", "Buildable", "Buildable",
                                      "Gameplay"));
  assert(editorComponentMatchesSearch("GAME", "Buildable", "Buildable",
                                      "Gameplay"));
  assert(editorComponentMatchesSearch("transform3", "Transform3D",
                                      "Transform 3D", "3D"));
  assert(!editorComponentMatchesSearch("network", "Buildable", "Buildable",
                                       "Gameplay"));

  const auto *descriptor =
      runtime::scene_loading::findComponentDescriptor("Transform3D");
  assert(descriptor != nullptr);
  const auto field = std::ranges::find(
      descriptor->fields, "position", &runtime::ComponentFieldDescriptor::name);
  assert(field != descriptor->fields.end());
  assert(runtime::scene_loading::componentFieldEditorLabel(*field) ==
         "Position");
  assert(runtime::scene_loading::componentFieldEditorStep(*field) > 0.0);
  const nlohmann::json schema =
      runtime::scene_loading::componentSchema(*descriptor);
  assert(schema["properties"]["position"].contains("default"));
  assert(schema["properties"]["position"]["x-demi-editor-label"] == "Position");
  return 0;
}
