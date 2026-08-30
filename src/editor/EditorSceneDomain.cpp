#include "editor/EditorSceneDomain.h"

#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/model/World.h"

namespace demi::editor {

EditorSceneDomain detectEditorSceneDomain(const runtime::World &world) {
  bool has2D = false;
  bool has3D = false;
  for (const runtime::Entity &entity : world.entities) {
    for (const runtime::scene_loading::ComponentDescriptor &descriptor :
         runtime::scene_loading::componentDescriptors()) {
      if (!descriptor.contains(entity))
        continue;
      has2D |= descriptor.domain == runtime::ComponentDomain::TwoDimensional;
      has3D |= descriptor.domain == runtime::ComponentDomain::ThreeDimensional;
    }
  }
  if (has2D && has3D)
    return EditorSceneDomain::Mixed;
  if (has2D)
    return EditorSceneDomain::TwoDimensional;
  if (has3D)
    return EditorSceneDomain::ThreeDimensional;
  return EditorSceneDomain::Empty;
}

EditorSceneViewDimension
defaultEditorSceneViewDimension(const EditorSceneDomain domain) {
  return domain == EditorSceneDomain::TwoDimensional
             ? EditorSceneViewDimension::TwoDimensional
             : EditorSceneViewDimension::ThreeDimensional;
}

} // namespace demi::editor
