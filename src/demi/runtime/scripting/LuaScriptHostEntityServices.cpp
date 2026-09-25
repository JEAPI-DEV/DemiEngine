#include "demi/runtime/network/ReplicatedState.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/camera/Camera3DMath.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/physics/Physics3D.h"
#include "demi/runtime/scripting/LuaScriptHostInternal.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <utility>

namespace demi::runtime {
std::optional<RigidbodyState3D> LuaScriptHost::rigidbodyState3D(const std::string &entityId) const {
  const auto *entity = lookupServiceEntity(entityId);
  const auto *body = entity ? entity->component<Rigidbody3DComponent>() : nullptr;
  if (!body) return std::nullopt;
  return RigidbodyState3D{.bodyType=body->bodyType, .mass=body->mass,
      .useGravity=body->useGravity, .enabled=body->bodyEnabled,
      .velocity=body->velocity,
      .angularVelocity=body->angularVelocity};
}

Entity *LuaScriptHost::lookupServiceEntity(const std::string &id) {
  if (Entity *pending = worldCommands_.pendingEntity(id)) {
    return pending;
  }
  return world_ ? serviceEntityLookup_.find(world_->entities, id) : nullptr;
}

const Entity *LuaScriptHost::lookupServiceEntity(const std::string &id) const {
  if (const Entity *pending = worldCommands_.pendingEntity(id)) {
    return pending;
  }
  return world_ ? serviceEntityLookup_.find(std::as_const(world_->entities), id)
                : nullptr;
}

input::GameplayInputService &LuaScriptHost::gameplayInput() {
  return gameplayInput_;
}

const InputState *LuaScriptHost::inputState() const { return input_; }

const std::vector<input::GestureEvent> &LuaScriptHost::gestures() const {
  return gestureEvents_;
}

std::string LuaScriptHost::textEntered() const {
  return input_ != nullptr ? input_->textEntered : std::string{};
}

bool LuaScriptHost::addEntityPosition(const std::string &entityId,
                                      const float dx, const float dy) {
  if (world_ == nullptr) {
    return false;
  }
  Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform2DComponent>()) {
    return false;
  }
  entity->component<Transform2DComponent>()->position.x += dx;
  entity->component<Transform2DComponent>()->position.y += dy;
  return true;
}

bool LuaScriptHost::setEntityPosition(const std::string &entityId,
                                      const float x, const float y) {
  if (world_ == nullptr) {
    return false;
  }
  Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform2DComponent>()) {
    return false;
  }
  entity->component<Transform2DComponent>()->position = Vec2{.x = x, .y = y};
  return true;
}

std::optional<Vec2>
LuaScriptHost::entityPosition(const std::string &entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  const Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform2DComponent>()) {
    return std::nullopt;
  }
  return entity->component<Transform2DComponent>()->position;
}

std::optional<float>
LuaScriptHost::entityRotation(const std::string &entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  const Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform2DComponent>()) {
    return std::nullopt;
  }
  return entity->component<Transform2DComponent>()->rotation;
}

bool LuaScriptHost::setEntityRotation(const std::string &entityId,
                                      const float rotation) {
  if (world_ == nullptr) {
    return false;
  }
  Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform2DComponent>()) {
    return false;
  }
  entity->component<Transform2DComponent>()->rotation = rotation;
  return true;
}

std::optional<Vec2>
LuaScriptHost::entityScale(const std::string &entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  const Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform2DComponent>()) {
    return std::nullopt;
  }
  return entity->component<Transform2DComponent>()->scale;
}

bool LuaScriptHost::setEntityScale(const std::string &entityId, const float x,
                                   const float y) {
  if (world_ == nullptr) {
    return false;
  }
  Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform2DComponent>()) {
    return false;
  }
  entity->component<Transform2DComponent>()->scale = Vec2{.x = x, .y = y};
  return true;
}

bool LuaScriptHost::addEntityPosition3D(const std::string &entityId,
                                        const float dx, const float dy,
                                        const float dz) {
  if (world_ == nullptr) {
    return false;
  }
  Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform3DComponent>()) {
    return false;
  }
  auto &position = entity->component<Transform3DComponent>()->position;
  position = {position.x + dx, position.y + dy, position.z + dz};
  return true;
}

bool LuaScriptHost::setEntityPosition3D(const std::string &entityId,
                                        const float x, const float y,
                                        const float z) {
  if (world_ == nullptr) {
    return false;
  }
  Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform3DComponent>()) {
    return false;
  }
  entity->component<Transform3DComponent>()->position = {x, y, z};
  return true;
}

std::optional<Vec3>
LuaScriptHost::entityPosition3D(const std::string &entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  const Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform3DComponent>()) {
    return std::nullopt;
  }
  return entity->component<Transform3DComponent>()->position;
}

std::optional<Vec3>
LuaScriptHost::entityRotation3D(const std::string &entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  const Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform3DComponent>()) {
    return std::nullopt;
  }
  return entity->component<Transform3DComponent>()->rotation;
}

bool LuaScriptHost::setEntityRotation3D(const std::string &entityId,
                                        const float x, const float y,
                                        const float z) {
  if (world_ == nullptr) {
    return false;
  }
  Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform3DComponent>()) {
    return false;
  }
  entity->component<Transform3DComponent>()->rotation =
      Vec3{.x = x, .y = y, .z = z};
  return true;
}

std::optional<Vec3>
LuaScriptHost::entityScale3D(const std::string &entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  const Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform3DComponent>()) {
    return std::nullopt;
  }
  return entity->component<Transform3DComponent>()->scale;
}

bool LuaScriptHost::setEntityScale3D(const std::string &entityId, const float x,
                                     const float y, const float z) {
  if (world_ == nullptr) {
    return false;
  }
  Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<Transform3DComponent>()) {
    return false;
  }
  entity->component<Transform3DComponent>()->scale =
      Vec3{.x = x, .y = y, .z = z};
  return true;
}

std::optional<Vec3>
LuaScriptHost::entityForward3D(const std::string &entityId) const {
  const Entity *entity =
      world_ != nullptr ? findEntity(*world_, entityId) : nullptr;
  const auto transform = entity != nullptr
                             ? resolveWorldTransform3D(*world_, *entity)
                             : std::nullopt;
  return transform ? std::optional{forwardDirection3D(*transform)}
                   : std::nullopt;
}

std::optional<Vec3>
LuaScriptHost::entityRight3D(const std::string &entityId) const {
  const Entity *entity =
      world_ != nullptr ? findEntity(*world_, entityId) : nullptr;
  const auto transform = entity != nullptr
                             ? resolveWorldTransform3D(*world_, *entity)
                             : std::nullopt;
  return transform ? std::optional{rightDirection3D(*transform)} : std::nullopt;
}

std::optional<Vec3>
LuaScriptHost::entityUp3D(const std::string &entityId) const {
  const Entity *entity =
      world_ != nullptr ? findEntity(*world_, entityId) : nullptr;
  const auto transform = entity != nullptr
                             ? resolveWorldTransform3D(*world_, *entity)
                             : std::nullopt;
  return transform ? std::optional{upDirection3D(*transform)} : std::nullopt;
}

bool LuaScriptHost::lookAtEntity3D(const std::string &entityId, const float x,
                                   const float y, const float z) {
  Entity *entity = world_ != nullptr ? lookupServiceEntity(entityId) : nullptr;
  auto *transform =
      entity != nullptr ? entity->component<Transform3DComponent>() : nullptr;
  if (transform == nullptr)
    return false;
  auto worldTransform = resolveWorldTransform3D(*world_, *entity);
  if (!worldTransform)
    return false;
  worldTransform->rotation =
      lookAtRotation3D(worldTransform->position, Vec3{x, y, z});
  const auto localTransform =
      worldToLocalTransform3D(*world_, *entity, *worldTransform);
  if (!localTransform)
    return false;
  transform->rotation = localTransform->rotation;
  return true;
}

std::optional<CameraRay3D>
LuaScriptHost::cameraRay3D(const std::string &entityId, const float screenX,
                           const float screenY, const float viewportWidth,
                           const float viewportHeight) const {
  const Entity *entity =
      world_ != nullptr ? findEntity(*world_, entityId) : nullptr;
  const auto *camera =
      entity != nullptr ? entity->component<Camera3DComponent>() : nullptr;
  const auto transform = entity != nullptr
                             ? resolveWorldTransform3D(*world_, *entity)
                             : std::nullopt;
  if (camera == nullptr || !transform)
    return std::nullopt;
  return cameraScreenRay3D(*transform, *camera, {screenX, screenY},
                           {viewportWidth, viewportHeight});
}

std::optional<Vec2> LuaScriptHost::cameraWorldToScreen3D(
    const std::string &entityId, const float worldX, const float worldY,
    const float worldZ, const float viewportWidth,
    const float viewportHeight) const {
  const Entity *entity =
      world_ != nullptr ? findEntity(*world_, entityId) : nullptr;
  const auto *camera =
      entity != nullptr ? entity->component<Camera3DComponent>() : nullptr;
  const auto transform = entity != nullptr
                             ? resolveWorldTransform3D(*world_, *entity)
                             : std::nullopt;
  return camera != nullptr && transform
             ? worldToScreen3D(*transform, *camera, {worldX, worldY, worldZ},
                               {viewportWidth, viewportHeight})
             : std::nullopt;
}

std::optional<Vec3> LuaScriptHost::cameraScreenToWorld3D(
    const std::string &entityId, const float screenX, const float screenY,
    const float viewportWidth, const float viewportHeight,
    const float distance) const {
  const Entity *entity =
      world_ != nullptr ? findEntity(*world_, entityId) : nullptr;
  const auto *camera =
      entity != nullptr ? entity->component<Camera3DComponent>() : nullptr;
  const auto transform = entity != nullptr
                             ? resolveWorldTransform3D(*world_, *entity)
                             : std::nullopt;
  return camera != nullptr && transform
             ? std::optional{screenToWorld3D(
                   *transform, *camera, {screenX, screenY},
                   {viewportWidth, viewportHeight}, distance)}
             : std::nullopt;
}

std::optional<std::string>
LuaScriptHost::findEntityId(const std::string &idOrName) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  if (const Entity *pending = worldCommands_.pendingEntity(idOrName)) {
    return pending->id;
  }
  for (const Entity &entity : world_->entities) {
    if (entity.id == idOrName || entity.name == idOrName) {
      return entity.id;
    }
  }
  return std::nullopt;
}

bool LuaScriptHost::setEntitySpriteColor(const std::string &entityId,
                                         const Color color) {
  if (world_ == nullptr) {
    return false;
  }
  Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr || !entity->hasComponent<SpriteComponent>()) {
    return false;
  }
  entity->component<SpriteComponent>()->color = color;
  return true;
}

std::optional<Vec2>
LuaScriptHost::getRigidbodyVelocity(const std::string &entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  return rigidbodyVelocity(*world_, entityId);
}

bool LuaScriptHost::setRigidbodyVelocity(const std::string &entityId,
                                         const float x, const float y) {
  return world_ != nullptr && demi::runtime::setRigidbodyVelocity(
                                  *world_, entityId, Vec2{.x = x, .y = y});
}

bool LuaScriptHost::setRigidbodyVelocityX(const std::string &entityId,
                                          const float x) {
  return world_ != nullptr &&
         demi::runtime::setRigidbodyVelocityX(*world_, entityId, x);
}

bool LuaScriptHost::setRigidbodyVelocityY(const std::string &entityId,
                                          const float y) {
  return world_ != nullptr &&
         demi::runtime::setRigidbodyVelocityY(*world_, entityId, y);
}

bool LuaScriptHost::addRigidbodyImpulse(const std::string &entityId,
                                        const float x, const float y) {
  return world_ != nullptr && demi::runtime::addRigidbodyImpulse(
                                  *world_, entityId, Vec2{.x = x, .y = y});
}

bool LuaScriptHost::addRigidbodyForce(const std::string &entityId,
                                      const float x, const float y) {
  return world_ != nullptr && demi::runtime::addRigidbodyForce(
                                  *world_, entityId, Vec2{.x = x, .y = y});
}

bool LuaScriptHost::addRigidbodyTorque(const std::string &entityId,
                                       const float torque) {
  return world_ != nullptr &&
         demi::runtime::addRigidbodyTorque(*world_, entityId, torque);
}

bool LuaScriptHost::setRigidbodyAngularVelocity(const std::string &entityId,
                                                const float angularVelocity) {
  return world_ != nullptr && demi::runtime::setRigidbodyAngularVelocity(
                                  *world_, entityId, angularVelocity);
}

bool LuaScriptHost::setRigidbodyAwake(const std::string &entityId,
                                      const bool awake) {
  return world_ != nullptr &&
         demi::runtime::setRigidbodyAwake(*world_, entityId, awake);
}

bool LuaScriptHost::setRigidbodyEnabled(const std::string &entityId,
                                        const bool enabled) {
  return world_ != nullptr &&
         demi::runtime::setRigidbodyEnabled(*world_, entityId, enabled);
}

bool LuaScriptHost::setRigidbodyContinuous(const std::string &entityId,
                                           const bool continuous) {
  return world_ != nullptr &&
         demi::runtime::setRigidbodyContinuous(*world_, entityId, continuous);
}

bool LuaScriptHost::setRigidbodyReportContacts(const std::string &entityId,
                                               const bool reportContacts) {
  return world_ != nullptr && demi::runtime::setRigidbodyReportContacts(
                                  *world_, entityId, reportContacts);
}

bool LuaScriptHost::moveKinematicBody(const std::string &entityId,
                                      const float x, const float y,
                                      const float fixedDt) {
  return world_ != nullptr &&
         demi::runtime::moveKinematicBody(*world_, entityId, {x, y}, fixedDt);
}

std::optional<Vec3>
LuaScriptHost::getRigidbodyVelocity3D(const std::string &entityId) const {
  return world_ != nullptr ? rigidbodyVelocity3D(*world_, entityId)
                           : std::nullopt;
}

bool LuaScriptHost::setRigidbodyVelocity3D(const std::string &entityId,
                                           const float x, const float y,
                                           const float z) {
  return world_ != nullptr && demi::runtime::setRigidbodyVelocity3D(
                                  *world_, entityId, Vec3{x, y, z});
}

bool LuaScriptHost::addRigidbodyImpulse3D(const std::string &entityId,
                                          const float x, const float y,
                                          const float z) {
  return world_ != nullptr &&
         demi::runtime::addRigidbodyImpulse3D(*world_, entityId, Vec3{x, y, z});
}

bool LuaScriptHost::addRigidbodyForce3D(const std::string &entityId,
                                        const float x, const float y,
                                        const float z) {
  return world_ != nullptr &&
         demi::runtime::addRigidbodyForce3D(*world_, entityId, Vec3{x, y, z});
}

bool LuaScriptHost::addRigidbodyTorque3D(const std::string &entityId,
                                         const float x, const float y,
                                         const float z) {
  return world_ != nullptr &&
         demi::runtime::addRigidbodyTorque3D(*world_, entityId, Vec3{x, y, z});
}

bool LuaScriptHost::setRigidbodyAwake3D(const std::string &entityId,
                                        const bool awake) {
  return world_ != nullptr &&
         demi::runtime::setRigidbodyAwake3D(*world_, entityId, awake);
}

bool LuaScriptHost::setRigidbodyEnabled3D(const std::string &entityId,
                                          const bool enabled) {
  return world_ != nullptr &&
         demi::runtime::setRigidbodyEnabled3D(*world_, entityId, enabled);
}

bool LuaScriptHost::setRigidbodyContinuous3D(const std::string &entityId,
                                             const bool continuous) {
  return world_ != nullptr &&
         demi::runtime::setRigidbodyContinuous3D(*world_, entityId, continuous);
}

bool LuaScriptHost::setRigidbodyReportContacts3D(const std::string &entityId,
                                                 const bool reportContacts) {
  return world_ != nullptr && demi::runtime::setRigidbodyReportContacts3D(
                                  *world_, entityId, reportContacts);
}

bool LuaScriptHost::moveKinematicBody3D(const std::string &entityId,
                                        const float x, const float y,
                                        const float z, const float rotationX,
                                        const float rotationY,
                                        const float rotationZ,
                                        const float fixedDt) {
  return world_ != nullptr &&
         demi::runtime::moveKinematicBody3D(
             *world_, entityId, Vec3{x, y, z},
             Vec3{rotationX, rotationY, rotationZ}, fixedDt);
}

bool LuaScriptHost::setCharacterVelocity3D(const std::string &entityId,
                                           const float x, const float y,
                                           const float z) {
  return world_ != nullptr && demi::runtime::setCharacterVelocity3D(
                                  *world_, entityId, Vec3{x, y, z});
}

bool LuaScriptHost::requestCharacterJump3D(const std::string &entityId,
                                           const float speed) {
  return world_ != nullptr &&
         demi::runtime::requestCharacterJump3D(*world_, entityId, speed);
}

std::optional<CharacterMoveResult3D>
LuaScriptHost::characterState3D(const std::string &entityId) const {
  return world_ != nullptr ? demi::runtime::characterState3D(*world_, entityId)
                           : std::nullopt;
}

std::optional<Vec2>
LuaScriptHost::moveAndSlideKinematic(const std::string &entityId, const float x,
                                     const float y) {
  if (world_ == nullptr)
    return std::nullopt;
  return demi::runtime::moveAndSlideKinematic(*world_, entityId, {x, y});
}

bool LuaScriptHost::physicsOverlapBox(
    const float x, const float y, const float width, const float height,
    const std::string &ignoredEntityId) const {
  return world_ != nullptr &&
         overlapBox(*world_, Vec2{.x = x, .y = y},
                    Vec2{.x = width, .y = height}, ignoredEntityId);
}

std::vector<std::string> LuaScriptHost::physicsOverlapCircle(
    const float x, const float y, const float radius, const std::string &layer,
    const std::string &ignoredEntityId) const {
  return world_ != nullptr ? overlapCircle(*world_, Vec2{.x = x, .y = y},
                                           radius, layer, ignoredEntityId)
                           : std::vector<std::string>{};
}

std::vector<PhysicsQueryHit2D> LuaScriptHost::physicsOverlapBoxAll(
    const float x, const float y, const float width, const float height,
    const std::string &layer, const std::string &ignoredEntityId) const {
  return world_ != nullptr ? overlapBoxAll(*world_, {x, y}, {width, height},
                                           layer, ignoredEntityId)
                           : std::vector<PhysicsQueryHit2D>{};
}

std::vector<PhysicsQueryHit2D> LuaScriptHost::physicsOverlapCircleAll(
    const float x, const float y, const float radius, const std::string &layer,
    const std::string &ignoredEntityId) const {
  return world_ != nullptr
             ? overlapCircleAll(*world_, {x, y}, radius, layer, ignoredEntityId)
             : std::vector<PhysicsQueryHit2D>{};
}

std::optional<PhysicsRaycastHit2D>
LuaScriptHost::physicsRaycast(const float originX, const float originY,
                              const float directionX, const float directionY,
                              const float distance, const std::string &layer,
                              const std::string &ignoredEntityId) const {
  return world_ == nullptr
             ? std::nullopt
             : raycast2D(*world_, {originX, originY}, {directionX, directionY},
                         distance, layer, ignoredEntityId);
}

std::vector<std::string> LuaScriptHost::physicsOverlapSphere3D(
    const float x, const float y, const float z, const float radius,
    const std::string &ignoredEntityId) const {
  return world_ != nullptr ? overlapSphere3D(*world_, {.x = x, .y = y, .z = z},
                                             radius, ignoredEntityId)
                           : std::vector<std::string>{};
}

std::vector<PhysicsQueryHit3D> LuaScriptHost::physicsOverlapSphereAll3D(
    const float x, const float y, const float z, const float radius,
    const std::string &layer, const std::string &ignoredEntityId) const {
  return world_ != nullptr ? overlapSphereAll3D(*world_, {x, y, z}, radius,
                                                layer, ignoredEntityId)
                           : std::vector<PhysicsQueryHit3D>{};
}

std::vector<PhysicsQueryHit3D> LuaScriptHost::physicsOverlapBoxAll3D(
    const float x, const float y, const float z, const float width,
    const float height, const float depth, const std::string &layer,
    const std::string &ignoredEntityId) const {
  return world_ != nullptr
             ? overlapBoxAll3D(*world_, {x, y, z}, {width, height, depth},
                               layer, ignoredEntityId)
             : std::vector<PhysicsQueryHit3D>{};
}

std::optional<PhysicsRaycastHit3D> LuaScriptHost::physicsRaycast3D(
    const float originX, const float originY, const float originZ,
    const float directionX, const float directionY, const float directionZ,
    const float distance, const std::string &ignoredEntityId) const {
  return world_ == nullptr
             ? std::nullopt
             : raycast3D(*world_, {.x = originX, .y = originY, .z = originZ},
                         {.x = directionX, .y = directionY, .z = directionZ},
                         distance, ignoredEntityId);
}

std::optional<PhysicsQueryHit3D> LuaScriptHost::physicsSphereCast3D(
    const float originX, const float originY, const float originZ,
    const float radius, const float directionX, const float directionY,
    const float directionZ, const float distance, const std::string &layer,
    const std::string &ignoredEntityId, bool includeTriggers) const {
  return world_ == nullptr
             ? std::nullopt
             : sphereCast3D(*world_, {originX, originY, originZ}, radius,
                            {directionX, directionY, directionZ}, distance,
                            layer, ignoredEntityId, includeTriggers);
}

bool LuaScriptHost::physicsHasContact(
    const std::string &entityId, const PhysicsContactFilter2D &filter) const {
  return world_ != nullptr && hasContact(*world_, entityId, filter);
}

std::vector<PhysicsContact2D>
LuaScriptHost::physicsContacts(const std::string &entityId) const {
  return world_ != nullptr ? contactsForEntity(*world_, entityId)
                           : std::vector<PhysicsContact2D>{};
}

std::optional<std::string>
LuaScriptHost::captureEntityReplicatedState(const std::string &entityId) const {
  if (world_ == nullptr)
    return std::nullopt;
  const Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr)
    return std::nullopt;
  return captureReplicatedState(*entity).dump();
}

std::optional<std::string> LuaScriptHost::captureEntityReplicatedState(
    const std::string &entityId, const NetworkContract &contract,
    const std::string_view prefabKey, const NetworkActor writer) const {
  if (world_ == nullptr)
    return std::nullopt;
  const Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr)
    return std::nullopt;
  return captureContractReplicatedState(*entity, contract, prefabKey, writer)
      .dump();
}

std::string
LuaScriptHost::applyEntityReplicatedState(const std::string &entityId,
                                          const std::string &stateJson) {
  if (world_ == nullptr)
    return "world is not loaded";
  Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr)
    return "entity not found: " + entityId;
  try {
    const ReplicatedStateResult result =
        applyReplicatedState(*entity, nlohmann::json::parse(stateJson));
    return result.ok ? std::string{} : result.error;
  } catch (const std::exception &error) {
    return std::string("invalid replicated state: ") + error.what();
  }
}

bool LuaScriptHost::setEntityMeshRenderer(
    const std::string &entityId, std::string texture, std::string material,
    std::string renderLayer, std::vector<Vec3> vertices,
    std::vector<Vec3> normals, std::vector<Vec2> uvs) {
  if (world_ == nullptr || entityId.empty()) {
    return false;
  }
  Entity *entity = lookupServiceEntity(entityId);
  if (entity == nullptr) {
    return false;
  }

  MeshRendererComponent mesh;
  if (entity->hasComponent<MeshRendererComponent>()) {
    mesh = *entity->component<MeshRendererComponent>();
  }
  mesh.texture = std::move(texture);
  mesh.material = std::move(material);
  mesh.renderLayer = std::move(renderLayer);
  mesh.vertices = std::move(vertices);
  mesh.normals = std::move(normals);
  mesh.uvs = std::move(uvs);
  mesh.markGeometryChanged();
  entity->setComponent<MeshRendererComponent>(std::move(mesh));
  return true;
}

} // namespace demi::runtime
