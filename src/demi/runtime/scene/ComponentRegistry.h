#pragma once

#include "demi/runtime/scene/Component2D.h"
#include "demi/runtime/scene/Component3D.h"
#include "demi/runtime/scene/GenericComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <memory>
#include <span>
#include <string_view>

namespace demi::runtime::scene_loading {

// Component identity is centralized here so loaders, validation, Lua bindings,
// and future editor tooling do not each define their own component-name list.
enum class ComponentType {
  Transform2D,
  Camera2D,
  Sprite,
  IsoGrid,
  IsoTransform,
  HitboxController,
  LuaScript,
  Buildable,
  Rigidbody2D,
  BoxCollider2D,
  Transform3D,
  Camera3D,
  MeshRenderer,
  AnimationPlayer3D,
  BoxCollider3D,
  SphereCollider3D,
  Rigidbody3D,
  DirectionalLight,
  AudioSource,
  AudioListener,
  VideoPlayer,
};

enum class ComponentDomain { Generic, TwoDimensional, ThreeDimensional };

using ComponentParseFn = void (*)(const Json &, Entity &);

struct ComponentDescriptor {
  ComponentType type;
  std::string_view name;
  ComponentParseFn parse;
  bool exposedToLua = false;
  ComponentDomain domain = ComponentDomain::Generic;
};

[[nodiscard]] std::span<const ComponentDescriptor> componentDescriptors();
[[nodiscard]] const ComponentDescriptor *
findComponentDescriptor(std::string_view name);
[[nodiscard]] std::shared_ptr<const Component>
makeAuthoredComponent(const ComponentDescriptor &descriptor, std::string json);

} // namespace demi::runtime::scene_loading
