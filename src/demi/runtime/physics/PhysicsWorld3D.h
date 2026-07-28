#pragma once

#include "demi/runtime/physics/Physics3DTypes.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

struct World;

class PhysicsWorld3D {
public:
  PhysicsWorld3D();
  ~PhysicsWorld3D();
  PhysicsWorld3D(const PhysicsWorld3D &) = delete;
  PhysicsWorld3D &operator=(const PhysicsWorld3D &) = delete;

  [[nodiscard]] bool available() const;
  void step(World &world, float fixedDt,
            Vec3 gravity = {0.0F, -9.81F, 0.0F});

  [[nodiscard]] bool setVelocity(const std::string &entityId, Vec3 velocity);
  [[nodiscard]] std::optional<Vec3>
  velocity(const std::string &entityId) const;
  [[nodiscard]] bool addForce(const std::string &entityId, Vec3 force);
  [[nodiscard]] bool addImpulse(const std::string &entityId, Vec3 impulse);
  [[nodiscard]] bool addTorque(const std::string &entityId, Vec3 torque);
  [[nodiscard]] bool setAwake(const std::string &entityId, bool awake);
  [[nodiscard]] bool setEnabled(const std::string &entityId, bool enabled);
  [[nodiscard]] bool setKinematicTarget(const std::string &entityId,
                                        Vec3 position, Vec3 rotation,
                                        float fixedDt);

  [[nodiscard]] std::optional<Vec3>
  interpolatedPosition(const std::string &entityId, float alpha) const;
  [[nodiscard]] std::vector<PhysicsQueryHit3D>
  overlapSphere(Vec3 center, float radius, const std::string &layer = {},
                const std::string &ignoredEntityId = {}) const;
  [[nodiscard]] std::vector<PhysicsQueryHit3D>
  overlapBox(Vec3 center, Vec3 size, const std::string &layer = {},
             const std::string &ignoredEntityId = {}) const;
  [[nodiscard]] std::vector<PhysicsQueryHit3D>
  overlapCapsule(Vec3 center, float radius, float height,
                 const std::string &layer = {},
                 const std::string &ignoredEntityId = {}) const;
  [[nodiscard]] std::optional<PhysicsQueryHit3D>
  raycast(Vec3 origin, Vec3 direction, float distance,
          const std::string &layer = {},
          const std::string &ignoredEntityId = {}) const;
  [[nodiscard]] std::optional<PhysicsQueryHit3D>
  castSphere(Vec3 origin, float radius, Vec3 direction, float distance,
             const std::string &layer = {},
             const std::string &ignoredEntityId = {}) const;
  [[nodiscard]] std::optional<PhysicsQueryHit3D>
  castCapsule(Vec3 origin, float radius, float height, Vec3 direction,
              float distance, const std::string &layer = {},
              const std::string &ignoredEntityId = {}) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] PhysicsWorld3D &ensurePhysicsWorld3D(World &world);

} // namespace demi::runtime
