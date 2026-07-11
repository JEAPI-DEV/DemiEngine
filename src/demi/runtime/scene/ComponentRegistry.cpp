#include "demi/runtime/scene/ComponentRegistry.h"

#include <array>

namespace demi::runtime::scene_loading {

namespace {

constexpr std::array Descriptors{
    makeComponentDescriptor<Transform2DComponent>(),
    makeComponentDescriptor<Camera2DComponent>(),
    makeComponentDescriptor<SpriteComponent>(),
    makeComponentDescriptor<IsoGridComponent>(),
    makeComponentDescriptor<IsoTransformComponent>(),
    makeComponentDescriptor<HitboxControllerComponent>(),
    makeComponentDescriptor<LuaScriptComponent>(),
    makeComponentDescriptor<BuildableComponent>(),
    makeComponentDescriptor<Rigidbody2DComponent>(),
    makeComponentDescriptor<BoxCollider2DComponent>(),
    makeComponentDescriptor<Transform3DComponent>(),
    makeComponentDescriptor<Camera3DComponent>(),
    makeComponentDescriptor<MeshRendererComponent>(),
    makeComponentDescriptor<AnimationPlayer3DComponent>(),
    makeComponentDescriptor<BoxCollider3DComponent>(),
    makeComponentDescriptor<SphereCollider3DComponent>(),
    makeComponentDescriptor<Rigidbody3DComponent>(),
    makeComponentDescriptor<DirectionalLightComponent>(),
    makeComponentDescriptor<AudioSourceComponent>(),
    makeComponentDescriptor<AudioListenerComponent>(),
    makeComponentDescriptor<VideoPlayerComponent>(),
};

} // namespace

std::span<const ComponentDescriptor> componentDescriptors() {
  return Descriptors;
}

const ComponentDescriptor *
findComponentDescriptor(const std::string_view name) {
  for (const ComponentDescriptor &descriptor : Descriptors) {
    if (descriptor.name == name) {
      return &descriptor;
    }
  }
  return nullptr;
}

std::shared_ptr<const Component>
makeAuthoredComponent(const ComponentDescriptor &descriptor, std::string json) {
  switch (descriptor.domain) {
  case ComponentDomain::TwoDimensional:
    return std::make_shared<Component2D>(std::string(descriptor.name),
                                         std::move(json));
  case ComponentDomain::ThreeDimensional:
    return std::make_shared<Component3D>(std::string(descriptor.name),
                                         std::move(json));
  case ComponentDomain::Generic:
    return std::make_shared<GenericComponent>(std::string(descriptor.name),
                                              std::move(json));
  }
  return std::make_shared<GenericComponent>(std::string(descriptor.name),
                                            std::move(json));
}

} // namespace demi::runtime::scene_loading
