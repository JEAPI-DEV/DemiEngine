#include "demi/runtime/terrain/TerrainScatterCollision.h"
#include "demi/runtime/scene/EntityPresets.h"
#include "demi/runtime/scene/components/3dcomponents/BoxCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/CapsuleCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/CharacterController3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ConvexCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ModelCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Rigidbody3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SphereCollider3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include <array>
#include <nlohmann/json.hpp>

namespace demi::runtime {
namespace {
template <class Component> void removePhysicsComponent(Entity &entity) {
  entity.removeComponent<Component>();
  entity.serializedComponents.erase(std::string(Component::typeName));
}

template <class Component>
void capturePhysics(const Entity &entity, ComponentStorage &authored) {
  if (const auto *component = entity.component<Component>()) {
    authored.set(*component);
  }
}

template <class Component>
void restorePhysics(Entity &entity, const ComponentStorage &authored) {
  if (const auto *component = authored.get<Component>()) {
    entity.setComponent(*component);
  }
}

template <class Collider>
void applyColliderPolicy(Entity &entity, TerrainCollisionPolicy policy) {
  if (policy == TerrainCollisionPolicy::None) {
    removePhysicsComponent<Collider>(entity);
    return;
  }
  if (auto *collider = entity.component<Collider>()) {
    collider->isTrigger = policy == TerrainCollisionPolicy::Trigger;
  }
}
} // namespace

nlohmann::json terrainScatterCollisionOverrides(const nlohmann::json &entity,
                                                TerrainCollisionPolicy policy) {
  using Json = nlohmann::json;
  const auto expanded = scene_loading::expandEntityPreset(entity);
  const auto components = expanded.find("components");
  Json overrides = Json::object();
  if (components == expanded.end() || !components->is_object()) {
    return overrides;
  }
  constexpr std::array colliderNames{"BoxCollider3D", "SphereCollider3D",
                                     "CapsuleCollider3D", "ConvexCollider3D",
                                     "ModelCollider3D"};
  for (const auto name : colliderNames) {
    if (!components->contains(name)) {
      continue;
    }
    overrides[name] =
        policy == TerrainCollisionPolicy::None
            ? Json(nullptr)
            : Json{{"is_trigger", policy == TerrainCollisionPolicy::Trigger}};
  }
  if (components->contains("Rigidbody3D")) {
    overrides["Rigidbody3D"] = policy == TerrainCollisionPolicy::None
                                   ? Json(nullptr)
                                   : Json{{"body_type", "static"}};
  }
  if (policy == TerrainCollisionPolicy::None &&
      components->contains("CharacterController3D")) {
    overrides["CharacterController3D"] = nullptr;
  }
  return overrides;
}

void applyTerrainScatterCollision(Entity &entity,
                                  TerrainCollisionPolicy policy) {
  auto *state = entity.component<TerrainScatterCollisionState>();
  if (!state) {
    TerrainScatterCollisionState initial;
    capturePhysics<BoxCollider3DComponent>(entity, initial.authored);
    capturePhysics<SphereCollider3DComponent>(entity, initial.authored);
    capturePhysics<CapsuleCollider3DComponent>(entity, initial.authored);
    capturePhysics<ConvexCollider3DComponent>(entity, initial.authored);
    capturePhysics<ModelCollider3DComponent>(entity, initial.authored);
    capturePhysics<Rigidbody3DComponent>(entity, initial.authored);
    capturePhysics<CharacterController3DComponent>(entity, initial.authored);
    state = &entity.setComponent(std::move(initial));
  } else if (state->policy != policy) {
    restorePhysics<BoxCollider3DComponent>(entity, state->authored);
    restorePhysics<SphereCollider3DComponent>(entity, state->authored);
    restorePhysics<CapsuleCollider3DComponent>(entity, state->authored);
    restorePhysics<ConvexCollider3DComponent>(entity, state->authored);
    restorePhysics<ModelCollider3DComponent>(entity, state->authored);
    restorePhysics<Rigidbody3DComponent>(entity, state->authored);
    restorePhysics<CharacterController3DComponent>(entity, state->authored);
  }
  state->policy = policy;
  applyColliderPolicy<BoxCollider3DComponent>(entity, policy);
  applyColliderPolicy<SphereCollider3DComponent>(entity, policy);
  applyColliderPolicy<CapsuleCollider3DComponent>(entity, policy);
  applyColliderPolicy<ConvexCollider3DComponent>(entity, policy);
  applyColliderPolicy<ModelCollider3DComponent>(entity, policy);
  if (policy == TerrainCollisionPolicy::None) {
    removePhysicsComponent<Rigidbody3DComponent>(entity);
    removePhysicsComponent<CharacterController3DComponent>(entity);
  } else if (auto *body = entity.component<Rigidbody3DComponent>()) {
    body->bodyType = "static";
  }
}
} // namespace demi::runtime
