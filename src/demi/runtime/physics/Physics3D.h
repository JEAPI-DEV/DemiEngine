#pragma once

#include "demi/runtime/physics/Physics3DTypes.h"
#include "demi/runtime/scene/model/World.h"

#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

[[nodiscard]] bool hasSolidCollider3D(const Entity &entity);
[[nodiscard]] bool collidesAt3D(const World &world, const Entity &moving,
                                Vec3 position);
[[nodiscard]] Vec3 resolveDynamicMove3D(const World &world,
                                        const Entity &entity, Vec3 from,
                                        Vec3 delta);
void stepPhysics3D(World &world, float fixedDt,
                   Vec3 gravity = {0.0F, -9.81F, 0.0F});
[[nodiscard]] bool setRigidbodyVelocity3D(World &world,
                                          const std::string &entityId,
                                          Vec3 velocity);
[[nodiscard]] std::optional<Vec3>
rigidbodyVelocity3D(const World &world, const std::string &entityId);
[[nodiscard]] bool addRigidbodyForce3D(World &world,
                                       const std::string &entityId, Vec3 force);
[[nodiscard]] bool addRigidbodyImpulse3D(World &world,
                                         const std::string &entityId,
                                         Vec3 impulse);
[[nodiscard]] bool addRigidbodyTorque3D(World &world,
                                        const std::string &entityId,
                                        Vec3 torque);
[[nodiscard]] bool setRigidbodyAwake3D(World &world,
                                       const std::string &entityId, bool awake);
[[nodiscard]] bool setRigidbodyEnabled3D(World &world,
                                         const std::string &entityId,
                                         bool enabled);
[[nodiscard]] bool moveKinematicBody3D(World &world,
                                       const std::string &entityId,
                                       Vec3 targetPosition,
                                       Vec3 targetRotation, float fixedDt);
[[nodiscard]] std::vector<PhysicsContact3D>
contactsForEntity3D(const World &world, const std::string &entityId);
[[nodiscard]] bool setCharacterVelocity3D(World &world,
                                          const std::string &entityId,
                                          Vec3 velocity);
[[nodiscard]] bool requestCharacterJump3D(World &world,
                                          const std::string &entityId,
                                          float speed);
[[nodiscard]] std::optional<CharacterMoveResult3D>
characterState3D(const World &world, const std::string &entityId);

} // namespace demi::runtime
