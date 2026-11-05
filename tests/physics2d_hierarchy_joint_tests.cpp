#include "demi/runtime/physics/Box2DWorldState.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/physics/PhysicsGeometry2D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <box2d/box2d.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace demi::runtime;
namespace {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
bool near(float left, float right, float tolerance = 0.0001F) {
  return std::abs(left - right) <= tolerance;
}
void step(World &world, float dt = 1.0F / 60) {
  stepPhysics2D(world, dt, PhysicsSettings2D{.gravity = {}});
}
b2World &native(World &world) {
  return *static_cast<b2World *>(world.box2dState->world);
}
Entity circle(std::string id, Vec2 position, const char *type = "static") {
  Entity entity;
  entity.id = std::move(id);
  entity.setComponent(Transform2DComponent{.position = position});
  entity.setComponent(CircleCollider2DComponent{.radius = 0.1F});
  entity.setComponent(
      Rigidbody2DComponent{.bodyType = type, .gravityScale = 0});
  return entity;
}
World jointFixture(const std::string &type) {
  World world;
  world.entities.push_back(circle("anchor", {}));
  auto bob = circle("bob", {1, 0}, "dynamic");
  if (type == "distance") {
    bob.setComponent(
        DistanceJoint2DComponent{.otherEntity = "anchor", .length = 1});
  } else {
    bob.setComponent(Joint2DComponent{
        .type = type, .otherEntity = "anchor", .anchor = {-1, 0}});
  }
  world.entities.push_back(std::move(bob));
  step(world);
  return world;
}
b2Joint &singleJoint(World &world) {
  expect(native(world).GetJointCount() == 1, "Expected one native joint");
  return *native(world).GetJointList();
}
void distanceEditsAndBodyLifetime() {
  auto world = jointFixture("distance");
  singleJoint(world).GetUserData().pointer = 73;
  auto *authored =
      findEntity(world, "bob")->component<DistanceJoint2DComponent>();
  authored->length = 0.5F;
  authored->stiffness = 4;
  authored->damping = 2;
  step(world);
  auto &distance = static_cast<b2DistanceJoint &>(singleJoint(world));
  expect(distance.GetUserData().pointer == 73 &&
             near(distance.GetLength(), 0.5F) &&
             near(distance.GetMinLength(), 0.5F) &&
             near(distance.GetMaxLength(), 0.5F) &&
             distance.GetStiffness() == 4 && distance.GetDamping() == 2,
         "Distance settings recreated the constraint or failed to update");
  authored->length = 2;
  step(world);
  expect(
      singleJoint(world).GetUserData().pointer == 73 &&
          near(
              static_cast<b2DistanceJoint &>(singleJoint(world)).GetMinLength(),
              2),
      "Growing the fixed distance lost its interval or native history");

  setRigidbodyEnabled(world, "bob", false);
  step(world);
  expect(singleJoint(world).GetUserData().pointer == 73,
         "Disabling a native body recreated its constraint");
  setRigidbodyEnabled(world, "bob", true);
  step(world);
  expect(singleJoint(world).GetUserData().pointer == 73,
         "Re-enabling a native body recreated its constraint");
  findEntity(world, "bob")->component<CircleCollider2DComponent>()->radius =
      0.2F;
  step(world);
  expect(
      singleJoint(world).GetUserData().pointer == 0,
      "Body replacement retained a joint attached to destroyed native storage");
  singleJoint(world).GetUserData().pointer = 19;
  findEntity(world, "anchor")->component<CircleCollider2DComponent>()->radius =
      0.2F;
  step(world);
  expect(singleJoint(world).GetUserData().pointer == 0,
         "Target body replacement retained a stale native constraint");
  world.entities.eraseIf(
      [](const Entity &entity) { return entity.id == "anchor"; });
  step(world);
  expect(native(world).GetJointCount() == 0,
         "Removed target retained a native joint");
  world.entities.push_back(circle("anchor", {}));
  step(world);
  singleJoint(world);
  findEntity(world, "bob")->removeComponent<DistanceJoint2DComponent>();
  step(world);
  expect(native(world).GetJointCount() == 0,
         "Removed component retained a native joint");
  findEntity(world, "bob")
      ->setComponent(
          DistanceJoint2DComponent{.otherEntity = "anchor", .length = 1});
  findEntity(world, "bob")
      ->setComponent(Joint2DComponent{
          .type = "rope", .otherEntity = "anchor", .maxLength = 3});
  step(world);
  expect(native(world).GetJointCount() == 2,
         "Two component types on one entity shared a native identity");
  findEntity(world, "bob")->enabled = false;
  step(world);
  expect(native(world).GetJointCount() == 0,
         "Disabled owner retained native joints");
  findEntity(world, "bob")->enabled = true;
  step(world);
  expect(native(world).GetJointCount() == 2,
         "Re-enabled owner lost joint components");
  // Exercise native owner retirement before world teardown and body slot reuse.
  Box2DWorldState moved(std::move(*world.box2dState));
  *world.box2dState = std::move(moved);
  step(world);
  expect(native(world).GetJointCount() == 2,
         "Moving native ownership invalidated joints");
}

void mutableJointParametersAndWeldReference() {
  for (const std::string type :
       {"revolute", "prismatic", "motor", "rope", "weld"}) {
    auto world = jointFixture(type);
    auto *source = findEntity(world, "bob")->component<Joint2DComponent>();
    singleJoint(world).GetUserData().pointer = 45;
    source->enableLimit = true;
    source->lowerLimit = -0.3F;
    source->upperLimit = 0.7F;
    source->enableMotor = true;
    source->motorSpeed = 2;
    source->maxMotorForce = 7;
    source->maxMotorTorque = 9;
    source->maxLength = 0.5F;
    source->correctionFactor = 0.5F;
    float weldReference = 0;
    if (type == "motor") {
      source->anchor = {0.25F, -0.1F};
      source->otherAnchor = {0.5F, 0.2F};
    }
    if (type == "weld") {
      weldReference =
          static_cast<b2WeldJoint &>(singleJoint(world)).GetReferenceAngle();
      findEntity(world, "bob")->component<Transform2DComponent>()->rotation +=
          0.4F;
    }
    step(world);
    auto &joint = singleJoint(world);
    expect(joint.GetUserData().pointer == 45,
           "Mutable settings discarded native joint history");
    if (type == "revolute") {
      const auto &value = static_cast<b2RevoluteJoint &>(joint);
      expect(value.IsLimitEnabled() && value.IsMotorEnabled() &&
                 near(value.GetLowerLimit(), -0.3F) &&
                 near(value.GetUpperLimit(), 0.7F) &&
                 value.GetMotorSpeed() == 2 && value.GetMaxMotorTorque() == 9,
             "Revolute parameters did not update");
    } else if (type == "prismatic") {
      const auto &value = static_cast<b2PrismaticJoint &>(joint);
      expect(value.IsLimitEnabled() && value.IsMotorEnabled() &&
                 near(value.GetLowerLimit(), -0.3F) &&
                 near(value.GetUpperLimit(), 0.7F) &&
                 value.GetMotorSpeed() == 2 && value.GetMaxMotorForce() == 7,
             "Prismatic parameters did not update");
    } else if (type == "rope") {
      expect(near(static_cast<b2DistanceJoint &>(joint).GetMaxLength(), 0.5F),
             "Rope maximum length did not update");
    } else if (type == "motor") {
      const auto &value = static_cast<b2MotorJoint &>(joint);
      expect(near(value.GetLinearOffset().x, 0.25F) &&
                 near(value.GetLinearOffset().y, 0.3F) &&
                 near(value.GetAngularOffset(), 0.7F) &&
                 value.GetMaxForce() == 7 && value.GetMaxTorque() == 9 &&
                 value.GetCorrectionFactor() == 0.5F,
             "Motor offsets or force settings did not update");
    } else {
      expect(near(static_cast<b2WeldJoint &>(joint).GetReferenceAngle(),
                  weldReference),
             "Weld reference angle followed a deformed pose instead of its "
             "bind pose");
    }
    if (type != "motor") {
      source->anchor.x += 0.1F;
      step(world);
      expect(singleJoint(world).GetUserData().pointer == 0,
             "Changing structural anchors retained an obsolete native joint");
    }
  }
}

void preservedJointAnchorFramesWithParents() {
  World world;
  Entity parent;
  parent.id = "parent";
  parent.setComponent(Transform2DComponent{
      .position = {10, 0}, .rotation = 1.57079632679F, .scale = {2, 3}});
  world.entities.push_back(std::move(parent));
  auto owner = circle("owner", {1, 0});
  owner.component<Transform2DComponent>()->parent = "parent";
  owner.setComponent(DistanceJoint2DComponent{
      .otherEntity = "target", .anchor = {1, 0}, .otherAnchor = {0, 2}});
  owner.setComponent(Joint2DComponent{.type = "weld",
                                      .otherEntity = "target",
                                      .anchor = {1, 0},
                                      .otherAnchor = {0, 2}});
  world.entities.push_back(std::move(owner));
  auto target = circle("target", {20, 0});
  target.component<Transform2DComponent>()->scale = {7, 8};
  world.entities.push_back(std::move(target));
  // Static bodies expose binding geometry without solver corrections moving
  // the independently authored anchor points into agreement.
  step(world);
  b2DistanceJoint *distance = nullptr;
  b2WeldJoint *weld = nullptr;
  const auto findJoints = [&] {
    distance = nullptr;
    weld = nullptr;
    for (auto *joint = native(world).GetJointList(); joint;
         joint = joint->GetNext()) {
      if (joint->GetType() == e_distanceJoint) {
        distance = static_cast<b2DistanceJoint *>(joint);
      } else if (joint->GetType() == e_weldJoint) {
        weld = static_cast<b2WeldJoint *>(joint);
      }
    }
    expect(distance && weld,
           "Parented anchor fixture did not create both joint kinds");
  };
  findJoints();
  const auto pose = resolveWorldTransform2D(world, *findEntity(world, "owner"));
  expect(pose && near(pose->position.x, 10) && near(pose->position.y, 2) &&
             near(distance->GetBodyA()->GetPosition().x, 10) &&
             near(distance->GetBodyA()->GetPosition().y, 2),
         "Parented joint owner did not use its composed world pose");
  expect(near(distance->GetAnchorA().x, 11) &&
             near(distance->GetAnchorA().y, 2) &&
             near(distance->GetLocalAnchorA().x, 0) &&
             near(distance->GetLocalAnchorA().y, -1),
         "Distance anchor did not bind its preserved world-axis offset");
  expect(near(weld->GetAnchorA().x, 10) && near(weld->GetAnchorA().y, 3) &&
             near(weld->GetLocalAnchorA().x, 1) &&
             near(weld->GetLocalAnchorA().y, 0) &&
             near(weld->GetAnchorB().x, 20) && near(weld->GetAnchorB().y, 2),
         "Generic joint anchors unexpectedly inherited authored scale");
  distance->GetUserData().pointer = 81;
  weld->GetUserData().pointer = 82;
  auto *parentTransform =
      findEntity(world, "parent")->component<Transform2DComponent>();
  parentTransform->position = {12, 1};
  parentTransform->rotation = 3.14159265359F;
  step(world);
  findJoints();
  expect(distance->GetUserData().pointer == 81 &&
             weld->GetUserData().pointer == 82,
         "Changing the parent pose discarded native anchor binding history");
  expect(
      near(distance->GetAnchorA().x, 10) && near(distance->GetAnchorA().y, 2) &&
          near(weld->GetAnchorA().x, 9) && near(weld->GetAnchorA().y, 1),
      "Bound anchors did not follow native local frames after parent rotation");
}

void nativeSolidColliderBounds() {
  World world;
  Entity collider;
  collider.id = "bounds";
  collider.setComponent(Transform2DComponent{});
  collider.setComponent(BoxCollider2DComponent{.size = {2, 4}});
  world.entities.push_back(std::move(collider));
  auto &entity = world.entities.front();
  const auto check = [&](float minX, float minY, float maxX, float maxY) {
    const auto bounds = physics2d_detail::solidColliderAabb(world, entity);
    expect(bounds && near(bounds->minX, minX) && near(bounds->minY, minY) &&
               near(bounds->maxX, maxX) && near(bounds->maxY, maxY),
           "Native solid bounds lost geometry or included solver skin");
  };
  check(-1, -2, 1, 2);
  entity.setComponent(
      CircleCollider2DComponent{.radius = 0.5F, .offset = {3, 0}});
  check(-1, -2, 3.5F, 2);
  entity.component<CircleCollider2DComponent>()->isTrigger = true;
  check(-1, -2, 1, 2);
  entity.component<BoxCollider2DComponent>()->isTrigger = true;
  entity.component<CircleCollider2DComponent>()->isTrigger = false;
  check(2.5F, -0.5F, 3.5F, 0.5F);
  entity.setComponent(Rigidbody2DComponent{.bodyType = "kinematic"});
  expect(moveAndSlideKinematic(world, "bounds", {1, 0}).has_value(),
         "A trigger collider hid another solid collider from movement");
  entity.component<CircleCollider2DComponent>()->isTrigger = true;
  expect(!moveAndSlideKinematic(world, "bounds", {1, 0}),
         "Trigger-only geometry blocked kinematic movement");
  entity.removeComponent<BoxCollider2DComponent>();
  entity.removeComponent<CircleCollider2DComponent>();
  entity.setComponent(CapsuleCollider2DComponent{.size = {2, 4}});
  entity.setComponent(Transform2DComponent{.rotation = 0.785398163F});
  const float extent = 1 + std::sqrt(0.5F);
  check(-extent, -extent, extent, extent);
  entity.removeComponent<CapsuleCollider2DComponent>();
  entity.setComponent(EdgeCollider2DComponent{.points = {{-2, 1}, {1, -3}, {4, 2}}});
  entity.setComponent(Transform2DComponent{});
  check(-2, -3, 4, 2);
}

void parentedSimulationAndQueries() {
  World world;
  Entity parent;
  parent.id = "parent";
  parent.setComponent(Transform2DComponent{
      .position = {10, 0}, .rotation = 1.57079632679F, .scale = {2, 1}});
  world.entities.push_back(std::move(parent));
  auto mover = circle("mover", {}, "dynamic");
  mover.component<Transform2DComponent>()->parent = "parent";
  mover.component<CircleCollider2DComponent>()->radius = 0.25F;
  mover.component<Rigidbody2DComponent>()->velocity = {4, 0};
  world.entities.push_back(std::move(mover));
  Entity wall;
  wall.id = "wall";
  wall.setComponent(Transform2DComponent{.position = {12, 0}});
  wall.setComponent(BoxCollider2DComponent{.size = {0.5F, 8}});
  world.entities.push_back(std::move(wall));
  for (int frame = 0; frame < 120; ++frame) {
    step(world);
  }
  auto *child = findEntity(world, "mover");
  auto pose = resolveWorldTransform2D(world, *child);
  expect(pose && pose->position.x > 11 && pose->position.x < 11.3F &&
             near(pose->position.y, 0),
         "Parented dynamic collision disagreed with composed scene geometry");
  const auto *body =
      static_cast<b2Body *>(world.box2dState->bodies.at("mover"));
  expect(
      near(body->GetPosition().x, pose->position.x) &&
          near(body->GetPosition().y, pose->position.y),
      "Native position did not publish through the inverse parent transform");
  const auto ray = raycast2D(world, {pose->position.x, 3}, {0, -1}, 5);
  expect(ray && ray->entityId == "mover" && near(ray->distance, 2.5F, 0.001F),
         "Native collision and authored queries disagreed on parent scale");
  const auto localScale = child->component<Transform2DComponent>()->scale;
  findEntity(world, "parent")->component<Transform2DComponent>()->position.x +=
      10;
  step(world);
  auto updated = resolveWorldTransform2D(world, *child);
  expect(updated && near(updated->position.x, pose->position.x + 10) &&
             child->component<Transform2DComponent>()->scale.x == localScale.x,
         "Immediate parent edits lost native placement or rewrote local scale");

  child->component<Rigidbody2DComponent>()->bodyType = "kinematic";
  expect(moveKinematicBody(world, "mover", {22, 1}, 0.25F),
         "Parented kinematic command failed");
  step(world, 0.25F);
  updated = resolveWorldTransform2D(world, *child);
  expect(updated && near(updated->position.x, 22) &&
             near(updated->position.y, 1),
         "Kinematic target did not use world coordinates");
  const auto applied = moveAndSlideKinematic(world, "mover", {-20, 0});
  expect(applied && applied->x > -10 && applied->x < 0,
         "Parented kinematic slide did not clip against world collider bounds");
}

void publicationOrderAndHierarchyValidation() {
  World world;
  auto child = circle("child", {2, 0}, "dynamic");
  child.component<Transform2DComponent>()->parent = "middle";
  child.component<CircleCollider2DComponent>()->radius = 0.05F;
  child.component<Rigidbody2DComponent>()->velocity = {2, 0};
  world.entities.push_back(std::move(child));
  Entity middle;
  middle.id = "middle";
  middle.setComponent(Transform2DComponent{
      .parent = "parent", .position = {1, 0}, .scale = {2, 1}});
  world.entities.push_back(std::move(middle));
  auto parent = circle("parent", {}, "dynamic");
  parent.component<Rigidbody2DComponent>()->velocity = {1, 0};
  world.entities.push_back(std::move(parent));
  step(world, 0.5F);
  const auto childPose = resolveWorldTransform2D(world, world.entities.front());
  expect(childPose && near(childPose->position.x, 6) &&
             near(world.entities.front()
                      .component<Transform2DComponent>()
                      ->position.x,
                  2.25F),
         "Child-before-parent publication composed old ancestor poses");
  findEntity(world, "middle")->component<Transform2DComponent>()->parent =
      "child";
  expect(!resolveWorldTransform2D(world, world.entities.front()),
         "Cyclic authored hierarchy did not terminate safely");
  const auto batch = resolveWorldTransforms2D(world);
  expect(!batch[0] && !batch[1],
         "Batch hierarchy accepted a cyclic parent chain");
  step(world);
  expect(!world.box2dState->bodies.contains("child"),
         "Invalid hierarchy retained stale native collision");
}
} // namespace

int main() {
  try {
    distanceEditsAndBodyLifetime();
    mutableJointParametersAndWeldReference();
    preservedJointAnchorFramesWithParents();
    nativeSolidColliderBounds();
    parentedSimulationAndQueries();
    publicationOrderAndHierarchyValidation();
    std::cout << "Persistent 2D joints and parented simulation passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
