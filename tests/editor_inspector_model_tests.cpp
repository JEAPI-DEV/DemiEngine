#include "editor/EditorInspectorModel.h"
#include "editor/EditorSceneDocument.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
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

  const auto project = std::filesystem::path(__FILE__).parent_path().parent_path() /
                       "examples/performance_3d_lab";
  const auto colliders = editorReferenceChoices(
      runtime::ComponentReferenceKind::Asset, project, {}, {}, {}, "ModelCollider3D");
  assert(colliders.size() == 1);
  assert(colliders.front().id == "asset://colliders/barrel");
  const auto assets = editorReferenceChoices(
      runtime::ComponentReferenceKind::Asset, project, {}, {}, {});
  assert(std::ranges::any_of(assets, [](const auto &choice) {
    return choice.id == "asset://models/barrel";
  }));

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

  assert(editorPropertyMatchesSearch("position", *descriptor, *field));
  assert(editorPropertyMatchesSearch("transform", *descriptor, *field));
  assert(!editorPropertyMatchesSearch("collider", *descriptor, *field));

  const nlohmann::json emptyTransform = nlohmann::json::object();
  const auto canonical =
      editorPropertyPresentation(*descriptor, *field, emptyTransform, false,
                                 false);
  assert(canonical.hasValue);
  assert(canonical.value == nlohmann::json({0.0, 0.0, 0.0}));
  assert(canonical.origin == EditorPropertyOrigin::Default);
  assert(!canonical.requiresExplicitAdd);
  assert(!canonical.canReset);

  const nlohmann::json authoredTransform = {
      {"position", {7.0, 8.0, 9.0}}};
  const auto authored = editorPropertyPresentation(
      *descriptor, *field, authoredTransform, false, true);
  assert(authored.origin == EditorPropertyOrigin::Authored);
  assert(authored.canReset);

  const auto inherited = editorPropertyPresentation(
      *descriptor, *field, authoredTransform, true, false);
  assert(inherited.origin == EditorPropertyOrigin::Inherited);
  assert(inherited.value == nlohmann::json({7.0, 8.0, 9.0}));
  assert(!inherited.canReset);

  const auto overridden = editorPropertyPresentation(
      *descriptor, *field, authoredTransform, true, true);
  assert(overridden.origin == EditorPropertyOrigin::Override);
  assert(overridden.canReset);

  const auto *animator =
      runtime::scene_loading::findComponentDescriptor("SpriteAnimator2D");
  assert(animator != nullptr);
  const auto atlas = std::ranges::find(
      animator->fields, "atlas", &runtime::ComponentFieldDescriptor::name);
  assert(atlas != animator->fields.end());
  const auto absentAtlas = editorPropertyPresentation(
      *animator, *atlas, nlohmann::json::object(), false, false);
  assert(!absentAtlas.hasValue);
  assert(absentAtlas.requiresExplicitAdd);
  assert(absentAtlas.origin == EditorPropertyOrigin::Missing);
  assert(editorPropertyOriginLabel(absentAtlas.origin) == "Not set");
  const auto absentInheritedAtlas = editorPropertyPresentation(
      *animator, *atlas, nlohmann::json::object(), true, false);
  assert(absentInheritedAtlas.origin == EditorPropertyOrigin::Missing);

  EditorSceneDocument document;
  std::string error;
  const auto defaultScenePath =
      std::filesystem::path(__FILE__).parent_path().parent_path() /
      "examples/destruction_weapons_3d_lab/scenes/main.scene.json";
  assert(document.open(defaultScenePath, error));
  const SceneValueTarget defaultPosition{
      .entityId = "world_stream",
      .component = "Transform3D",
      .field = "position"};
  const nlohmann::json *emptyComponent =
      document.component("world_stream", "Transform3D");
  assert(emptyComponent != nullptr && !emptyComponent->contains("position"));
  const auto editableDefault = editorPropertyPresentation(
      *descriptor, *field, *emptyComponent, false, false);
  assert(editableDefault.origin == EditorPropertyOrigin::Default);
  assert(document.setValue(defaultPosition, {2.0, 3.0, 4.0}, false, error));
  assert(document.component("world_stream", "Transform3D")->at("position") ==
         nlohmann::json({2.0, 3.0, 4.0}));
  assert(document.removeValue(defaultPosition, error));
  assert(!document.component("world_stream", "Transform3D")
              ->contains("position"));
  return 0;
}
