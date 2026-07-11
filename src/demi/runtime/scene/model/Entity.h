#pragma once

#include "demi/runtime/scene/Component.h"
#include "demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/IsoGridComponent.h"
#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Rigidbody2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/BoxCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/DirectionalLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Rigidbody3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SphereCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/components/gameplay/BuildableComponent.h"
#include "demi/runtime/scene/components/gameplay/HitboxControllerComponent.h"
#include "demi/runtime/scene/components/gameplay/LuaScriptComponent.h"
#include "demi/runtime/scene/components/media/AudioListenerComponent.h"
#include "demi/runtime/scene/components/media/AudioSourceComponent.h"
#include "demi/runtime/scene/components/media/VideoPlayerComponent.h"

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
