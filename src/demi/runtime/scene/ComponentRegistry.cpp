#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/ComponentParsers.h"

#include <array>

namespace demi::runtime::scene_loading {

namespace {

constexpr std::array Descriptors{
    ComponentDescriptor{ComponentType::Transform2D, "Transform2D",
                        component_parsing::parseTransform2D, true,
                        ComponentDomain::TwoDimensional},
    ComponentDescriptor{ComponentType::Camera2D, "Camera2D",
                        component_parsing::parseCamera2D, false,
                        ComponentDomain::TwoDimensional},
    ComponentDescriptor{ComponentType::Sprite, "Sprite",
                        component_parsing::parseSprite, false,
                        ComponentDomain::TwoDimensional},
    ComponentDescriptor{ComponentType::IsoGrid, "IsoGrid",
                        component_parsing::parseIsoGrid, false,
                        ComponentDomain::TwoDimensional},
    ComponentDescriptor{ComponentType::IsoTransform, "IsoTransform",
                        component_parsing::parseIsoTransform, false,
                        ComponentDomain::TwoDimensional},
    ComponentDescriptor{ComponentType::HitboxController, "HitboxController",
                        component_parsing::parseHitboxController},
    ComponentDescriptor{ComponentType::LuaScript, "LuaScript",
                        component_parsing::parseLuaScript},
    ComponentDescriptor{ComponentType::Buildable, "Buildable",
                        component_parsing::parseBuildable},
    ComponentDescriptor{ComponentType::Rigidbody2D, "Rigidbody2D",
                        component_parsing::parseRigidbody2D, true,
                        ComponentDomain::TwoDimensional},
    ComponentDescriptor{ComponentType::BoxCollider2D, "BoxCollider2D",
                        component_parsing::parseBoxCollider2D, false,
                        ComponentDomain::TwoDimensional},
    ComponentDescriptor{ComponentType::Transform3D, "Transform3D",
                        component_parsing::parseTransform3D, true,
                        ComponentDomain::ThreeDimensional},
    ComponentDescriptor{ComponentType::Camera3D, "Camera3D",
                        component_parsing::parseCamera3D, false,
                        ComponentDomain::ThreeDimensional},
    ComponentDescriptor{ComponentType::MeshRenderer, "MeshRenderer",
                        component_parsing::parseMeshRenderer, false,
                        ComponentDomain::ThreeDimensional},
    ComponentDescriptor{ComponentType::AnimationPlayer3D, "AnimationPlayer3D",
                        component_parsing::parseAnimationPlayer3D, false,
                        ComponentDomain::ThreeDimensional},
    ComponentDescriptor{ComponentType::BoxCollider3D, "BoxCollider3D",
                        component_parsing::parseBoxCollider3D, false,
                        ComponentDomain::ThreeDimensional},
    ComponentDescriptor{ComponentType::SphereCollider3D, "SphereCollider3D",
                        component_parsing::parseSphereCollider3D, false,
                        ComponentDomain::ThreeDimensional},
    ComponentDescriptor{ComponentType::Rigidbody3D, "Rigidbody3D",
                        component_parsing::parseRigidbody3D, false,
                        ComponentDomain::ThreeDimensional},
    ComponentDescriptor{ComponentType::DirectionalLight, "DirectionalLight",
                        component_parsing::parseDirectionalLight, false,
                        ComponentDomain::ThreeDimensional},
    ComponentDescriptor{ComponentType::AudioSource, "AudioSource",
                        component_parsing::parseAudioSource},
    ComponentDescriptor{ComponentType::AudioListener, "AudioListener",
                        component_parsing::parseAudioListener},
    ComponentDescriptor{ComponentType::VideoPlayer, "VideoPlayer",
                        component_parsing::parseVideoPlayer},
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
