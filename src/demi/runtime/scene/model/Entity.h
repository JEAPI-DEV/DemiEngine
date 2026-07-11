#pragma once

#include "demi/runtime/scene/Component.h"
#include "demi/runtime/scene/components/Transform2DComponent.h"
#include "demi/runtime/scene/components/gameplay/GameplayComponents.h"
#include "demi/runtime/scene/components/media/MediaComponents.h"
#include "demi/runtime/scene/components/three_dimensional/PhysicsComponents.h"
#include "demi/runtime/scene/components/three_dimensional/RenderComponents.h"
#include "demi/runtime/scene/components/three_dimensional/Transform3DComponent.h"
#include "demi/runtime/scene/components/two_dimensional/PhysicsComponents.h"
#include "demi/runtime/scene/components/two_dimensional/RenderComponents.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct Entity {
  std::string id;
  std::string name;
  std::unordered_map<std::string, std::string> serializedComponents;
  std::vector<std::shared_ptr<const Component>> authoredComponents;
  std::optional<Transform2DComponent> transform2D;
  std::optional<Camera2DComponent> camera2D;
  std::optional<SpriteComponent> sprite;
  std::optional<IsoGridComponent> isoGrid;
  std::optional<IsoTransformComponent> isoTransform;
  std::optional<HitboxControllerComponent> hitboxController;
  std::optional<LuaScriptComponent> luaScript;
  std::optional<BuildableComponent> buildable;
  std::optional<Rigidbody2DComponent> rigidbody2D;
  std::optional<BoxCollider2DComponent> boxCollider2D;
  std::optional<Transform3DComponent> transform3D;
  std::optional<Camera3DComponent> camera3D;
  std::optional<MeshRendererComponent> meshRenderer;
  std::optional<AnimationPlayer3DComponent> animationPlayer3D;
  std::optional<BoxCollider3DComponent> boxCollider3D;
  std::optional<SphereCollider3DComponent> sphereCollider3D;
  std::optional<Rigidbody3DComponent> rigidbody3D;
  std::optional<DirectionalLightComponent> directionalLight;
  std::optional<AudioSourceComponent> audioSource;
  std::optional<AudioListenerComponent> audioListener;
  std::optional<VideoPlayerComponent> videoPlayer;
};

[[nodiscard]] inline const std::string *
serializedComponent(const Entity &entity, const std::string &name) {
  const auto iterator = entity.serializedComponents.find(name);
  return iterator == entity.serializedComponents.end() ? nullptr
                                                       : &iterator->second;
}

} // namespace demi::runtime
