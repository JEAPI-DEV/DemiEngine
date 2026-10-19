#pragma once
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include <cstdint>
#include <map>
namespace demi::runtime {
// Render-only copies of the entity's shared static mesh. No per-copy entities.
struct MeshInstances3DComponent {
  static constexpr std::string_view typeName = "MeshInstances3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"transforms", ComponentFieldType::Object}
          .withoutAssetReferences()
          .withHelp(
              "Render-only local instances keyed by stable labels. Each "
              "supports position, rotation and scale. Uses the owner's shared "
              "static MeshRenderer; no per-instance colliders or scripts.")};
  static constexpr ComponentEditorMetadata editor{"3D", "Mesh Instances 3D"};
  static void parse(const nlohmann::json &, Entity &);
  static nlohmann::json defaults();
  MeshInstances3DComponent();
  MeshInstances3DComponent(const MeshInstances3DComponent &) = default;
  MeshInstances3DComponent &operator=(const MeshInstances3DComponent &other);
  MeshInstances3DComponent(MeshInstances3DComponent &&other) noexcept;
  MeshInstances3DComponent &
  operator=(MeshInstances3DComponent &&other) noexcept;

  const std::map<std::string, WorldTransform3D> &transforms() const {
    return transforms_;
  }
  std::uint64_t contentRevision() const { return contentRevision_; }
  void setTransform(std::string label, const WorldTransform3D &transform);
  bool eraseTransform(const std::string &label);
  void clearTransforms();

  static void copyTransforms(MeshInstances3DComponent &destination,
                             const MeshInstances3DComponent &source);
  static bool readTransforms(const MeshInstances3DComponent &source,
                             nlohmann::json &out);
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<MeshInstances3DComponent>{
          "transforms", copyTransforms, readTransforms}};

private:
  // Equal tokens guarantee equal owned values, even across world copies and
  // component replacement. Mutable map access would break renderer reuse.
  std::map<std::string, WorldTransform3D> transforms_;
  std::uint64_t contentRevision_;
};
} // namespace demi::runtime
