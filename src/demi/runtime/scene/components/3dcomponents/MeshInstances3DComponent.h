#pragma once
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include <map>
namespace demi::runtime {
// Render-only copies of the entity's shared static mesh. No per-copy entities.
struct MeshInstances3DComponent {
  static constexpr std::string_view typeName = "MeshInstances3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"transforms", ComponentFieldType::Object}
          .withHelp(
              "Render-only local instances keyed by stable labels. Each "
              "supports position, rotation and scale. Uses the owner's shared "
              "static MeshRenderer; no per-instance colliders or scripts.")};
  static constexpr ComponentEditorMetadata editor{"3D", "Mesh Instances 3D"};
  static void parse(const nlohmann::json &, Entity &);
  static nlohmann::json defaults();
  std::map<std::string, WorldTransform3D> transforms;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<MeshInstances3DComponent>::member<
          &MeshInstances3DComponent::transforms>("transforms")};
};
} // namespace demi::runtime
