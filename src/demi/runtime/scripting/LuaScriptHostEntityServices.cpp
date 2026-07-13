#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/input/InputActionResolver.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/physics/Physics3D.h"
#include "demi/runtime/scripting/LuaScriptHostInternal.h"

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>

namespace demi::runtime {

bool LuaScriptHost::isKeyDown(const std::string &key) const {
  return input_ != nullptr && input_->keysDown.contains(normalizedKey(key));
}

bool LuaScriptHost::isKeyPressed(const std::string &key) const {
  return input_ != nullptr && input_->keysPressed.contains(normalizedKey(key));
}

bool LuaScriptHost::isActionDown(const std::string &action) const {
  return input_ != nullptr &&
         input::InputActionResolver{}.down(inputActions_, *input_, action);
}

bool LuaScriptHost::isActionPressed(const std::string &action) const {
  return input_ != nullptr &&
         input::InputActionResolver{}.pressed(inputActions_, *input_, action);
}

float LuaScriptHost::actionValue(const std::string &action) const {
  return input_ == nullptr ? 0.0F
                           : input::InputActionResolver{}.value(
                                 inputActions_, *input_, action);
}

std::string LuaScriptHost::textEntered() const {
  return input_ != nullptr ? input_->textEntered : std::string{};
}

bool LuaScriptHost::addEntityPosition(const std::string &entityId,
                                      const float dx, const float dy) {
  if (world_ == nullptr) {
    return false;
  }
  Entity *entity = findEntity(*world_, entityId);
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
  Entity *entity = findEntity(*world_, entityId);
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
  const Entity *entity = findEntity(*world_, entityId);
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
  const Entity *entity = findEntity(*world_, entityId);
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
  Entity *entity = findEntity(*world_, entityId);
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
  const Entity *entity = findEntity(*world_, entityId);
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
  Entity *entity = findEntity(*world_, entityId);
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
  Entity *entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->hasComponent<Transform3DComponent>()) {
    return false;
  }
  entity->component<Transform3DComponent>()->position = resolveDynamicMove3D(
      *world_, *entity, entity->component<Transform3DComponent>()->position,
      Vec3{.x = dx, .y = dy, .z = dz});
  return true;
}

bool LuaScriptHost::setEntityPosition3D(const std::string &entityId,
                                        const float x, const float y,
                                        const float z) {
  if (world_ == nullptr) {
    return false;
  }
  Entity *entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->hasComponent<Transform3DComponent>()) {
    return false;
  }
  const Vec3 target{.x = x, .y = y, .z = z};
  const Vec3 delta{
      .x = target.x - entity->component<Transform3DComponent>()->position.x,
      .y = target.y - entity->component<Transform3DComponent>()->position.y,
      .z = target.z - entity->component<Transform3DComponent>()->position.z};
  entity->component<Transform3DComponent>()->position = resolveDynamicMove3D(
      *world_, *entity, entity->component<Transform3DComponent>()->position,
      delta);
  return true;
}

std::optional<Vec3>
LuaScriptHost::entityPosition3D(const std::string &entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  const Entity *entity = findEntity(*world_, entityId);
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
  const Entity *entity = findEntity(*world_, entityId);
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
  Entity *entity = findEntity(*world_, entityId);
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
  const Entity *entity = findEntity(*world_, entityId);
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
  Entity *entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->hasComponent<Transform3DComponent>()) {
    return false;
  }
  entity->component<Transform3DComponent>()->scale =
      Vec3{.x = x, .y = y, .z = z};
  return true;
}

std::optional<std::string>
LuaScriptHost::findEntityId(const std::string &idOrName) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  for (const Entity &entity : world_->entities) {
    if (entity.id == idOrName || entity.name == idOrName) {
      return entity.id;
    }
  }
  return std::nullopt;
}

bool LuaScriptHost::destroyEntity(const std::string &entityId) {
  if (world_ == nullptr) {
    return false;
  }
  const auto before = world_->entities.size();
  std::erase_if(world_->entities,
                [&](const Entity &entity) { return entity.id == entityId; });
  return world_->entities.size() != before;
}

int LuaScriptHost::destroyEntities(const std::vector<std::string> &entityIds) {
  if (world_ == nullptr || entityIds.empty()) {
    return 0;
  }
  std::unordered_set<std::string> ids;
  ids.reserve(entityIds.size());
  for (const std::string &entityId : entityIds) {
    if (!entityId.empty()) {
      ids.insert(entityId);
    }
  }
  if (ids.empty()) {
    return 0;
  }
  const auto before = world_->entities.size();
  std::erase_if(world_->entities,
                [&](const Entity &entity) { return ids.contains(entity.id); });
  return static_cast<int>(before - world_->entities.size());
}

bool LuaScriptHost::setEntitySpriteColor(const std::string &entityId,
                                         const Color color) {
  if (world_ == nullptr) {
    return false;
  }
  Entity *entity = findEntity(*world_, entityId);
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

bool LuaScriptHost::physicsHasContact(
    const std::string &entityId, const PhysicsContactFilter2D &filter) const {
  return world_ != nullptr && hasContact(*world_, entityId, filter);
}

std::vector<PhysicsContact2D>
LuaScriptHost::physicsContacts(const std::string &entityId) const {
  return world_ != nullptr ? contactsForEntity(*world_, entityId)
                           : std::vector<PhysicsContact2D>{};
}

bool LuaScriptHost::createEntity(Entity entity) {
  if (world_ == nullptr || entity.id.empty()) {
    return false;
  }
  if (entity.name.empty()) {
    entity.name = entity.id;
  }
  if (Entity *existing = findEntity(*world_, entity.id)) {
    *existing = std::move(entity);
    return true;
  }
  world_->entities.push_back(std::move(entity));
  return true;
}

bool LuaScriptHost::setEntityMeshRenderer(const std::string &entityId,
                                          std::string texture,
                                          std::vector<Vec3> vertices,
                                          std::vector<Vec3> normals,
                                          std::vector<Vec2> uvs) {
  if (world_ == nullptr || entityId.empty()) {
    return false;
  }
  Entity *entity = findEntity(*world_, entityId);
  if (entity == nullptr) {
    return false;
  }

  MeshRendererComponent mesh;
  if (entity->hasComponent<MeshRendererComponent>()) {
    mesh = *entity->component<MeshRendererComponent>();
  }
  mesh.texture = std::move(texture);
  mesh.vertices = std::move(vertices);
  mesh.normals = std::move(normals);
  mesh.uvs = std::move(uvs);
  mesh.revision = nextMeshRevision_++;
  mesh.hasBounds = !mesh.vertices.empty();
  if (mesh.hasBounds) {
    mesh.boundsMin = mesh.vertices.front();
    mesh.boundsMax = mesh.vertices.front();
    for (const Vec3 &vertex : mesh.vertices) {
      mesh.boundsMin.x = std::min(mesh.boundsMin.x, vertex.x);
      mesh.boundsMin.y = std::min(mesh.boundsMin.y, vertex.y);
      mesh.boundsMin.z = std::min(mesh.boundsMin.z, vertex.z);
      mesh.boundsMax.x = std::max(mesh.boundsMax.x, vertex.x);
      mesh.boundsMax.y = std::max(mesh.boundsMax.y, vertex.y);
      mesh.boundsMax.z = std::max(mesh.boundsMax.z, vertex.z);
    }
  }
  entity->setComponent<MeshRendererComponent>(std::move(mesh));
  return true;
}

} // namespace demi::runtime
