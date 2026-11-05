#include "demi/runtime/physics/Box2DJoints2D.h"

#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"

#include <box2d/box2d.h>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <type_traits>

namespace demi::runtime {
namespace {
bool same(Vec2 left, Vec2 right) {
  return left.x == right.x && left.y == right.y;
}
bool same(b2Vec2 left, b2Vec2 right) {
  return left.x == right.x && left.y == right.y;
}

struct JointStructure {
  b2Body *bodyA;
  b2Body *bodyB;
  std::string type;
  Vec2 anchor;
  Vec2 otherAnchor;
  Vec2 axis{};
  bool collideConnected;

  bool matches(const JointStructure &other) const {
    return bodyA == other.bodyA && bodyB == other.bodyB && type == other.type &&
           same(anchor, other.anchor) && same(otherAnchor, other.otherAnchor) &&
           same(axis, other.axis) && collideConnected == other.collideConnected;
  }
};

b2Joint *createDistanceJoint(b2World &world, const JointStructure &structure,
                             const DistanceJoint2DComponent &source) {
  b2DistanceJointDef definition;
  const auto positionA = structure.bodyA->GetPosition();
  const auto positionB = structure.bodyB->GetPosition();
  definition.Initialize(structure.bodyA, structure.bodyB,
                        positionA + b2Vec2(source.anchor.x, source.anchor.y),
                        positionB +
                            b2Vec2(source.otherAnchor.x, source.otherAnchor.y));
  definition.length = source.length;
  definition.minLength = source.length;
  definition.maxLength = source.length;
  definition.stiffness = source.stiffness;
  definition.damping = source.damping;
  definition.collideConnected = source.collideConnected;
  return world.CreateJoint(&definition);
}

b2Joint *createJoint(b2World &world, const JointStructure &structure,
                     const Joint2DComponent &source) {
  const auto publish = [&](auto &definition) {
    definition.bodyA = structure.bodyA;
    definition.bodyB = structure.bodyB;
    definition.collideConnected = source.collideConnected;
    return world.CreateJoint(&definition);
  };
  const auto setAnchors = [&](auto &definition) {
    definition.localAnchorA = {source.anchor.x, source.anchor.y};
    definition.localAnchorB = {source.otherAnchor.x, source.otherAnchor.y};
  };
  if (source.type == "revolute") {
    b2RevoluteJointDef definition;
    setAnchors(definition);
    definition.enableLimit = source.enableLimit;
    definition.lowerAngle = source.lowerLimit;
    definition.upperAngle = source.upperLimit;
    definition.enableMotor = source.enableMotor;
    definition.motorSpeed = source.motorSpeed;
    definition.maxMotorTorque = source.maxMotorTorque;
    return publish(definition);
  }
  if (source.type == "prismatic") {
    b2PrismaticJointDef definition;
    setAnchors(definition);
    definition.localAxisA = {source.axis.x, source.axis.y};
    definition.enableLimit = source.enableLimit;
    definition.lowerTranslation = source.lowerLimit;
    definition.upperTranslation = source.upperLimit;
    definition.enableMotor = source.enableMotor;
    definition.motorSpeed = source.motorSpeed;
    definition.maxMotorForce = source.maxMotorForce;
    return publish(definition);
  }
  if (source.type == "weld") {
    b2WeldJointDef definition;
    setAnchors(definition);
    definition.referenceAngle =
        structure.bodyB->GetAngle() - structure.bodyA->GetAngle();
    return publish(definition);
  }
  if (source.type == "rope") {
    b2DistanceJointDef definition;
    setAnchors(definition);
    definition.length = source.maxLength;
    definition.minLength = 0.0F;
    definition.maxLength = source.maxLength;
    return publish(definition);
  }
  if (source.type == "motor") {
    b2MotorJointDef definition;
    definition.Initialize(structure.bodyA, structure.bodyB);
    definition.linearOffset = {source.otherAnchor.x - source.anchor.x,
                               source.otherAnchor.y - source.anchor.y};
    definition.angularOffset = source.upperLimit;
    definition.maxForce = source.maxMotorForce;
    definition.maxTorque = source.maxMotorTorque;
    definition.correctionFactor = source.correctionFactor;
    return publish(definition);
  }
  return nullptr;
}

void updateDistanceJoint(b2Joint &native,
                         const DistanceJoint2DComponent &source) {
  auto &joint = static_cast<b2DistanceJoint &>(native);
  const float length = std::max(source.length, b2_linearSlop);
  const bool changed = joint.GetLength() != length ||
                       joint.GetStiffness() != source.stiffness ||
                       joint.GetDamping() != source.damping;
  if (joint.GetLength() != length) {
    // Adjust the interval in a valid order when its fixed length decreases.
    joint.SetMinLength(std::min(joint.GetMinLength(), length));
    joint.SetMaxLength(length);
    joint.SetMinLength(length);
    joint.SetLength(length);
  }
  joint.SetStiffness(source.stiffness);
  joint.SetDamping(source.damping);
  if (changed) {
    native.GetBodyA()->SetAwake(true);
    native.GetBodyB()->SetAwake(true);
  }
}

void updateJoint(b2Joint &native, const Joint2DComponent &source) {
  if (source.type == "revolute") {
    auto &joint = static_cast<b2RevoluteJoint &>(native);
    joint.EnableLimit(source.enableLimit);
    joint.SetLimits(source.lowerLimit, source.upperLimit);
    joint.EnableMotor(source.enableMotor);
    joint.SetMotorSpeed(source.motorSpeed);
    joint.SetMaxMotorTorque(source.maxMotorTorque);
  } else if (source.type == "prismatic") {
    auto &joint = static_cast<b2PrismaticJoint &>(native);
    joint.EnableLimit(source.enableLimit);
    joint.SetLimits(source.lowerLimit, source.upperLimit);
    joint.EnableMotor(source.enableMotor);
    joint.SetMotorSpeed(source.motorSpeed);
    joint.SetMaxMotorForce(source.maxMotorForce);
  } else if (source.type == "rope") {
    auto &joint = static_cast<b2DistanceJoint &>(native);
    const float maximumLength = std::max(source.maxLength, b2_linearSlop);
    if (joint.GetMaxLength() != maximumLength) {
      joint.SetLength(maximumLength);
      joint.SetMaxLength(maximumLength);
      native.GetBodyA()->SetAwake(true);
      native.GetBodyB()->SetAwake(true);
    }
  } else if (source.type == "motor") {
    auto &joint = static_cast<b2MotorJoint &>(native);
    const b2Vec2 offset(source.otherAnchor.x - source.anchor.x,
                        source.otherAnchor.y - source.anchor.y);
    const bool changed = !same(joint.GetLinearOffset(), offset) ||
                         joint.GetAngularOffset() != source.upperLimit ||
                         joint.GetMaxForce() != source.maxMotorForce ||
                         joint.GetMaxTorque() != source.maxMotorTorque ||
                         joint.GetCorrectionFactor() != source.correctionFactor;
    if (changed) {
      joint.SetLinearOffset(offset);
      joint.SetAngularOffset(source.upperLimit);
      joint.SetMaxForce(source.maxMotorForce);
      joint.SetMaxTorque(source.maxMotorTorque);
      joint.SetCorrectionFactor(source.correctionFactor);
      native.GetBodyA()->SetAwake(true);
      native.GetBodyB()->SetAwake(true);
    }
  }
}
} // namespace

struct Box2DJoints2D::Impl {
  struct Record {
    JointStructure structure;
    b2Joint *native;
    bool seen = false;
  };
  explicit Impl(b2World &nativeWorld) : world(nativeWorld) {}
  b2World &world;
  // One entity can own both component types; their native identities differ.
  std::map<std::string, std::array<std::optional<Record>, 2>> owners;

  void destroy(std::optional<Record> &record) {
    if (record) {
      world.DestroyJoint(record->native);
      record.reset();
    }
  }
};

Box2DJoints2D::Box2DJoints2D(b2World &world)
    : impl_(std::make_unique<Impl>(world)) {}
Box2DJoints2D::~Box2DJoints2D() {
  for (auto &[id, records] : impl_->owners) {
    for (auto &record : records) {
      impl_->destroy(record);
    }
  }
}

void Box2DJoints2D::removeBody(const b2Body &body) {
  for (auto &[id, records] : impl_->owners) {
    for (auto &record : records) {
      if (record && (record->structure.bodyA == &body ||
                     record->structure.bodyB == &body)) {
        impl_->destroy(record);
      }
    }
  }
}

void Box2DJoints2D::synchronize(
    const World &world, std::span<const std::size_t> entityIndices,
    const std::unordered_map<std::string, void *> &bodies) {
  for (auto &[id, records] : impl_->owners) {
    for (auto &record : records) {
      if (record) {
        record->seen = false;
      }
    }
  }
  const auto bodyFor = [&](const std::string &id) -> b2Body * {
    const auto found = bodies.find(id);
    return found == bodies.end() ? nullptr
                                 : static_cast<b2Body *>(found->second);
  };
  for (const auto index : entityIndices) {
    const auto &entity = world.entities[index];
    if (!entity.enabled) {
      continue;
    }
    const auto synchronize = [&](const auto &source, std::size_t kind) {
      auto *bodyA = bodyFor(entity.id);
      auto *bodyB = bodyFor(source.otherEntity);
      if (!bodyA || !bodyB || bodyA == bodyB) {
        return;
      }
      JointStructure structure{.bodyA = bodyA,
                               .bodyB = bodyB,
                               .type = "distance",
                               .anchor = source.anchor,
                               .otherAnchor = source.otherAnchor,
                               .collideConnected = source.collideConnected};
      if constexpr (std::is_same_v<std::decay_t<decltype(source)>,
                                   Joint2DComponent>) {
        structure.type = source.type;
        if (source.type == "motor") {
          structure.anchor = {};
          structure.otherAnchor = {};
        }
        if (source.type == "prismatic") {
          structure.axis = source.axis;
        }
      }
      auto &record = impl_->owners[entity.id][kind];
      if (record && !record->structure.matches(structure)) {
        impl_->destroy(record);
      }
      if (!record) {
        b2Joint *native;
        if constexpr (std::is_same_v<std::decay_t<decltype(source)>,
                                     DistanceJoint2DComponent>) {
          native = createDistanceJoint(impl_->world, structure, source);
        } else {
          native = createJoint(impl_->world, structure, source);
        }
        if (!native) {
          return;
        }
        record = Impl::Record{std::move(structure), native, true};
        native->GetBodyA()->SetAwake(true);
        native->GetBodyB()->SetAwake(true);
      }
      record->seen = true;
      if constexpr (std::is_same_v<std::decay_t<decltype(source)>,
                                   DistanceJoint2DComponent>) {
        updateDistanceJoint(*record->native, source);
      } else {
        updateJoint(*record->native, source);
      }
    };
    if (const auto *source = entity.component<DistanceJoint2DComponent>()) {
      synchronize(*source, 0);
    }
    if (const auto *source = entity.component<Joint2DComponent>()) {
      synchronize(*source, 1);
    }
  }
  for (auto iterator = impl_->owners.begin();
       iterator != impl_->owners.end();) {
    for (auto &record : iterator->second) {
      if (record && !record->seen) {
        impl_->destroy(record);
      }
    }
    if (!iterator->second[0] && !iterator->second[1]) {
      iterator = impl_->owners.erase(iterator);
    } else {
      ++iterator;
    }
  }
}
} // namespace demi::runtime
