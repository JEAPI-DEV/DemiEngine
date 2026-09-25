#include "demi/runtime/destruction/DestructionWorld3D.h"
#include "demi/runtime/destruction/DetachedFragmentFade3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scene/SceneFlow.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace demi::runtime;
namespace {
void expect(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}
ColliderPart3D cube(const std::string &id, float x) {
  ColliderPart3D part{.id = id};
  for (float dx : {-0.5F, 0.5F})
    for (float y : {-0.5F, 0.5F})
      for (float z : {-0.5F, 0.5F})
        part.points.push_back({x + dx, y, z});
  return part;
}
World fixture(bool anchored = true) {
  World world;
  Entity floor;
  floor.id = "floor";
  floor.setComponent(Transform3DComponent{.position = {0, -0.5F, 0}});
  floor.setComponent(BoxCollider3DComponent{.size = {40, 1, 40}});
  Rigidbody3DComponent floorBody;
  floorBody.bodyType = "static";
  floor.setComponent(floorBody);
  world.entities.push_back(std::move(floor));
  ColliderAsset3D asset;
  asset.parts = {cube("left", -1), cube("middle", 0), cube("right", 1)};
  asset.fracture = demi::assets::ColliderFractureGraph{
      .bonds = {{"a", "left", "middle", 1}, {"b", "middle", "right", 1}},
      .anchors = anchored ? std::vector<std::string>{"left"}
                          : std::vector<std::string>{}};
  world.colliderAssets3D.emplace("asset://assembly", asset);
  Entity root;
  root.id = "root";
  root.setComponent(Transform3DComponent{.position = {0, 4, 0}});
  root.setComponent(ModelCollider3DComponent{.asset = "asset://assembly"});
  Rigidbody3DComponent body;
  body.bodyType = anchored ? "static" : "dynamic";
  body.mass = 3;
  body.linearDamping = body.angularDamping = 0;
  body.useGravity = anchored;
  if (!anchored) {
    body.velocity = {3, 0, 0};
    body.angularVelocity = {0, 0, 2};
  }
  root.setComponent(body);
  Destructible3DComponent config;
  for (const auto &part : asset.parts)
    config.parts.emplace(part.id, "visual_" + part.id);
  root.setComponent(config);
  world.entities.push_back(std::move(root));
  for (const auto &part : asset.parts) {
    Entity visual;
    visual.id = "visual_" + part.id;
    float x = part.id == "left" ? -1 : part.id == "right" ? 1 : 0;
    visual.setComponent(
        Transform3DComponent{.parent = "root", .position = {x, 0, 0}});
    visual.setComponent(MeshRendererComponent{});
    world.entities.push_back(std::move(visual));
  }
  return world;
}
void hit(World &world, const std::string &part, float amount) {
  std::string error;
  expect(world.destruction3D->damagePart("root", part, amount, error), error);
}

void spatialImpacts() {
  auto world = fixture();
  auto &physics = ensurePhysicsWorld3D(world);
  const auto step = [&] { physics.step(world, 1.0F / 60, {0, 0, 0}); };
  step();
  std::string error;
  std::size_t affected = 0;
  DestructionImpact3D impact{.position = {1.49F, 4, 0},
                             .radius = 0.06F,
                             .energy = 1250,
                             .impulse = 5,
                             .direction = {0, 0, 1},
                             .entity = "root"};
  const auto contacts =
      physics.overlapSphere(impact.position, impact.radius, {}, {}, true);
  expect(contacts.size() == 1 && contacts.front().colliderPartId == "right",
         "Spatial query missed an edge hit or used body bounds instead of part "
         "geometry");
  expect(world.destruction3D->impact(world, physics, impact, affected, error) &&
             affected == 1,
         error);
  expect(world.destruction3D->state("root").revision == 0,
         "Spatial hit was not queued");
  step();
  const auto state = world.destruction3D->state("root");
  expect(state.status == "applied" && state.bodies == 2,
         "Localized energy failed to split: " + state.error);
  expect(state.parts.at("left") == state.parts.at("middle"),
         "Spatial hit damaged a distant bond");
  const auto *right = findEntity(world, state.parts.at("right"))
                          ->component<Rigidbody3DComponent>();
  expect(
      right->velocity.z > 1 && std::abs(right->angularVelocity.y) > 0.1F,
      "Committed off-center impulse did not produce translation and rotation");
  auto miss = impact;
  miss.position = {50, 4, 0};
  expect(!world.destruction3D->impact(world, physics, miss, affected, error) &&
             affected == 0,
         "Out-of-radius geometry accepted damage");
  miss.position.x = std::numeric_limits<float>::quiet_NaN();
  expect(!world.destruction3D->impact(world, physics, miss, affected, error),
         "NaN impact accepted");

  auto rotated = fixture();
  auto *transform =
      findEntity(rotated, "root")->component<Transform3DComponent>();
  transform->rotation.y = 1.5707963F;
  transform->scale = {2, 1, 1};
  impact.position = transformPoint3D(
      {transform->position, transform->rotation, transform->scale},
      {1.45F, 0, 0});
  impact.impulse = 0;
  auto &rotatedPhysics = ensurePhysicsWorld3D(rotated);
  rotatedPhysics.step(rotated, 1.0F / 60, {0, 0, 0});
  expect(rotated.destruction3D->impact(rotated, rotatedPhysics, impact,
                                       affected, error),
         error);
  rotatedPhysics.step(rotated, 1.0F / 60, {0, 0, 0});
  const auto rotatedState = rotated.destruction3D->state("root");
  expect(rotatedState.bodies == 2 &&
             rotatedState.parts.at("left") == rotatedState.parts.at("middle"),
         "Rotated/scaled assembly did not resolve a world-space impact");
}

void checkpointsAndCleanup() {
  std::string error;
  {
    auto partial=fixture();auto &p=ensurePhysicsWorld3D(partial);p.step(partial,1e-6F);
    hit(partial,"middle",.25F);p.step(partial,1e-6F);
    const auto checkpoint=partial.destruction3D->checkpoint(partial,"root",error);
    auto resumed=fixture();auto &q=ensurePhysicsWorld3D(resumed);q.step(resumed,1e-6F);
    expect(resumed.destruction3D->restore("root",checkpoint,error),error);q.step(resumed,1e-6F);
    hit(resumed,"middle",.8F);q.step(resumed,1e-6F);
    expect(resumed.destruction3D->state("root").bodies==3,"Restore lost partial bond damage");
  }
  auto world = fixture(false);
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.F / 60);
  hit(world, "middle", 2);
  physics.step(world, 1.F / 60);
  const auto saved = world.destruction3D->checkpoint(world, "root", error);
  expect(!saved.is_null() && saved.dump().size() < 2048, error);
  auto restored = fixture(false);
  auto &native = ensurePhysicsWorld3D(restored);
  native.step(restored, 1e-6F);
  expect(restored.destruction3D->restore("root", saved, error), error);
  native.step(restored, 1e-6F);
  auto state = restored.destruction3D->state("root");
  expect(state.status == "applied" && state.bodies == 3,
         "Checkpoint restore failed: " + state.error);
  for (const auto &group : saved["groups"]) {
    const auto part = group["parts"][0].get<std::string>();
    const auto *entity = findEntity(restored, state.parts.at(part));
    const auto *t = entity->component<Transform3DComponent>();
    const auto *b = entity->component<Rigidbody3DComponent>();
    expect(
        std::abs(t->position.x - group["position"][0].get<float>()) < .001F &&
            std::abs(b->velocity.y - group["velocity"][1].get<float>()) < .001F,
        "Checkpoint lost fragment pose/momentum");
  }
  expect(!restored.destruction3D->restore("root", saved, error),
         "Restore overwrote a live damaged assembly");

  auto anchored = fixture();
  auto &anchoredPhysics = ensurePhysicsWorld3D(anchored);
  anchoredPhysics.step(anchored, 1e-6F);
  hit(anchored, "middle", 2);
  anchoredPhysics.step(anchored, 1e-6F);
  expect(anchored.destruction3D->retireDebris("root", error), error);
  expect(anchored.destruction3D->checkpoint(anchored, "root", error).is_null(),
         "Checkpoint ignored queued cleanup");
  anchoredPhysics.step(anchored, 1e-6F);
  state = anchored.destruction3D->state("root");
  expect(state.bodies == 1 && state.parts.size() == 1 &&
             state.parts.contains("left"),
         "Cleanup removed support or retained loose bodies");
  const auto cleaned =
      anchored.destruction3D->checkpoint(anchored, "root", error);
  auto reload = fixture();
  findEntity(reload, "root")->component<Destructible3DComponent>()->maxBodies =
      1;
  auto &reloadPhysics = ensurePhysicsWorld3D(reload);
  reloadPhysics.step(reload, 1e-6F);
  auto bad = cleaned;
  bad["geometry_hash"] = "changed";
  expect(!reload.destruction3D->restore("root", bad, error),
         "Wrong template checkpoint accepted");
  expect(reload.destruction3D->restore("root", cleaned, error), error);
  reloadPhysics.step(reload, 1e-6F);
  state = reload.destruction3D->state("root");
  expect(state.status == "applied" && state.bodies == 1 &&
             state.parts.size() == 1,
         "Reload resurrected retired debris: " + state.error);
  expect(!reloadPhysics.raycast({1, 4, 5}, {0, 0, -1}, 10),
         "Retired debris still collides after restore");
}
void repeatedSleepingFragmentHits(bool heavy = false) {
  auto world = fixture();
  if (heavy)
    findEntity(world, "root")->component<Rigidbody3DComponent>()->mass = 1440;
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.F / 60);
  hit(world, "middle", 2);
  for (int step = 0; step < 600; ++step)
    physics.step(world, 1.F / 60);
  const auto owner = world.destruction3D->state("root").parts.at("right");
  for (int strike = 0; strike < 4; ++strike) {
    const auto *body = findEntity(world, owner)->component<Rigidbody3DComponent>();
    expect(!body->awake, "Loose fragment must settle and sleep before the next hit");
    const auto position = resolveWorldTransform3D(world, *findEntity(world, "visual_right"))->position;
    const auto revision = world.destruction3D->state("root").revision;
    DestructionImpact3D blow{.position = {position.x, position.y, position.z + .5F},
                            .radius = .15F, .energy = 18000, .impulse = heavy ? 220.F : 2.F,
                            .direction = {0, 0, -1}, .entity = owner};
    std::size_t affected = 0;
    std::string error;
    expect(world.destruction3D->impact(world, physics, blow, affected, error), error);
    physics.step(world, 1.F / 60);
    const auto state = world.destruction3D->state("root");
    expect(state.status == "applied" && state.revision == revision + 1,
           "Repeated impact failed after bonds were exhausted: " + state.error);
    expect(physics.velocity(owner)->z < (heavy ? -.2F : -.5F),
           "Repeated hammer hit failed to wake/push the sleeping fragment");
    for (int step = 0; step < 600; ++step)
      physics.step(world, 1.F / 60);
  }
}
void impactSupportAndRollback() {
  auto world = fixture();
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  hit(world, "middle", 2);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  std::size_t affected;
  std::string error;
  DestructionImpact3D hitBase{.position = {-1, 4, 0.5F},
                              .radius = 0.2F,
                              .energy = 1100,
                              .impulse = 2,
                              .direction = {0, 1, 0},
                              .entity = "root"};
  expect(world.destruction3D->impact(world, physics, hitBase, affected, error),
         error);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  auto state = world.destruction3D->state("root");
  expect(
      findEntity(world, state.parts.at("left"))
              ->component<Rigidbody3DComponent>()
              ->velocity.y > 1,
      "Spatial hit did not release an isolated foundation and apply impulse");
  auto loose = hitBase;
  loose.position.x = 1;
  loose.energy = 0;
  expect(world.destruction3D->impact(world, physics, loose, affected, error),
         error);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  expect(world.destruction3D->state("root").revision == state.revision + 1,
         "Impulse-only impact failed to finish without connected bonds");
  expect(findEntity(world, state.parts.at("right"))
                 ->component<Rigidbody3DComponent>()
                 ->velocity.y > 1,
         "Impulse-only hit did not move an already detached fragment");

  auto rollback = fixture(false);
  auto *config =
      findEntity(rollback, "root")->component<Destructible3DComponent>();
  config->maxBodies = 1;
  auto &native = ensurePhysicsWorld3D(rollback);
  native.step(rollback, 1.0F / 60, {0, 0, 0});
  auto hitRight = loose;
  hitRight.energy = 2000;
  expect(rollback.destruction3D->impact(rollback, native, hitRight, affected,
                                        error),
         error);
  native.step(rollback, 1.0F / 60, {0, 0, 0});
  expect(rollback.destruction3D->state("root").status == "failed" &&
             rollback.destruction3D->state("root").revision == 0 &&
             native.velocity("root")->y == 0,
         "Failed split applied an impulse or committed damage");
  config->maxBodies = 64;
  hitRight.energy = 600;
  hitRight.impulse = 0;
  expect(rollback.destruction3D->impact(rollback, native, hitRight, affected,
                                        error),
         error);
  native.step(rollback, 1.0F / 60, {0, 0, 0});
  expect(rollback.destruction3D->state("root").bodies == 1 &&
             native.velocity("root")->y == 0,
         "Cancelled spatial energy/impulse leaked into the next batch");
}

void impactStrengthAndFalloff() {
  auto world = fixture();
  world.colliderAssets3D.at("asset://assembly").fracture->bonds[1].health = 10;
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  std::size_t affected;
  std::string error;
  DestructionImpact3D hitRight{.position = {1, 4, 0.5F},
                               .radius = 0.2F,
                               .energy = 2000,
                               .entity = "root"};
  expect(world.destruction3D->impact(world, physics, hitRight, affected, error),
         error);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  expect(world.destruction3D->state("root").bodies == 1,
         "Strong connection ignored authored resistance");
  hitRight.energy = 9000;
  expect(world.destruction3D->impact(world, physics, hitRight, affected, error),
         error);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  expect(world.destruction3D->state("root").bodies == 2,
         "Repeated energy did not accumulate");

  auto distant = fixture();
  auto &native = ensurePhysicsWorld3D(distant);
  native.step(distant, 1.0F / 60, {0, 0, 0});
  hitRight.position = {1, 4, 0.65F};
  hitRight.radius = 0.2F;
  hitRight.energy = 2000;
  expect(
      distant.destruction3D->impact(distant, native, hitRight, affected, error),
      error);
  native.step(distant, 1.0F / 60, {0, 0, 0});
  expect(distant.destruction3D->state("root").bodies == 1,
         "Distance falloff was normalized away for a single affected bond");

  auto scaled = fixture();
  findEntity(scaled, "root")
      ->component<Destructible3DComponent>()
      ->energyPerHealth = 4000;
  auto &scaledPhysics = ensurePhysicsWorld3D(scaled);
  scaledPhysics.step(scaled, 1.0F / 60, {0, 0, 0});
  hitRight.position.z = 0.5F;
  expect(scaled.destruction3D->impact(scaled, scaledPhysics, hitRight, affected,
                                      error),
         error);
  scaledPhysics.step(scaled, 1.0F / 60, {0, 0, 0});
  expect(scaled.destruction3D->state("root").bodies == 1,
         "energy_per_health was ignored");
  hitRight.energy = 2100;
  expect(scaled.destruction3D->impact(scaled, scaledPhysics, hitRight, affected,
                                      error),
         error);
  scaledPhysics.step(scaled, 1.0F / 60, {0, 0, 0});
  expect(scaled.destruction3D->state("root").bodies == 2,
         "Scaled cumulative energy failed to fracture");
}

void impactAcrossAssemblies() {
  auto world = fixture();
  auto second = fixture();
  for (auto &entity : second.entities) {
    if (entity.id == "floor")
      continue;
    entity.id = "other_" + entity.id;
    if (auto *t = entity.component<Transform3DComponent>()) {
      if (!t->parent.empty())
        t->parent = "other_" + t->parent;
      else
        t->position.x += 6;
    }
    if (auto *d = entity.component<Destructible3DComponent>())
      for (auto &[part, visual] : d->parts)
        visual = "other_" + visual;
    world.entities.push_back(std::move(entity));
  }
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  DestructionImpact3D blast{
      .position = {3, 4, 0}, .radius = 10, .energy = 1500};
  std::size_t affected;
  std::string error;
  for (int i = 0; i < 2; ++i) {
    expect(
        world.destruction3D->impact(world, physics, blast, affected, error) &&
            affected == 2,
        error);
    physics.step(world, 1.0F / 60, {0, 0, 0});
    physics.step(world, 1.0F / 60, {0, 0, 0});
  }
  expect(
      world.destruction3D->state("root").revision == 2 &&
          world.destruction3D->state("other_root").revision == 2 &&
          world.destruction3D->state("root").bodies == 1 &&
          world.destruction3D->state("other_root").bodies == 1,
      "Explosion energy was multiplied per assembly or queued work was lost");
  blast.entity = "root";
  blast.energy = 10000;
  expect(world.destruction3D->impact(world, physics, blast, affected, error) &&
             affected == 1,
         error);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  expect(world.destruction3D->state("other_root").revision == 2,
         "Target filter affected another assembly");
}

void realFragmentFading() {
  auto world=fixture();
  findEntity(world,"root")->component<Destructible3DComponent>()->fadingParts["right"]={3,1};
  auto &physics=ensurePhysicsWorld3D(world);
  physics.step(world,1.F/60,{0,0,0});
  std::string error;
  expect(!findEntity(world,"visual_right")->hasComponent<FragmentOpacity3D>(),"Intact visual fades");
  expect(world.destruction3D->damagePart("root","right",2,error),error);
  physics.step(world,1.F/60,{0,0,0});
  auto *visual=findEntity(world,"visual_right");
  expect(visual && visual->hasComponent<FragmentOpacity3D>(),"Detached real visual missing fade");
  const auto body=visual->component<FragmentOpacity3D>()->body;
  expect(!physics.velocity(body) && !findEntity(world,body)->hasComponent<Rigidbody3DComponent>(),"Fading fragment retained collision/body");
  expect(findEntity(world,"visual_left") && !findEntity(world,"visual_left")->hasComponent<FragmentOpacity3D>(),"Physical neighbor began fading");
  const auto saved=world.destruction3D->checkpoint(world,"root",error);
  expect(!saved.is_null(),error);
  updateDetachedFragmentFades3D(world,2.5F,{});
  expect(std::abs(findEntity(world,"visual_right")->component<FragmentOpacity3D>()->value-.5F)<1e-5F,"Real shard did not fade");
  updateDetachedFragmentFades3D(world,1,{});
  expect(!findEntity(world,"visual_right") && !findEntity(world,body),"Expired real shard leaked");
  auto restored=fixture();
  findEntity(restored,"root")->component<Destructible3DComponent>()->fadingParts["right"]={3,1};
  auto &native=ensurePhysicsWorld3D(restored);native.step(restored,1.F/60,{0,0,0});
  expect(restored.destruction3D->restore("root",saved,error),error);
  native.step(restored,1.F/60,{0,0,0});
  expect(!findEntity(restored,"visual_right"),"Checkpoint respawned disposable geometry");
}

void cosmeticImpactEffects() {
  auto world=fixture();
  findEntity(world,"root")->setComponent(FractureDebris3DComponent{.count=4,.maxFragments=6});
  auto &physics=ensurePhysicsWorld3D(world);
  physics.step(world,1.F/60,{0,0,0});
  const auto entities=world.entities.size();
  const auto overlaps=physics.overlapSphere({1.49F,4,0},.06F,{}, {},true).size();
  std::size_t affected=0;
  std::string error;
  DestructionImpact3D hit{.position={1.49F,4,0},.radius=.06F,.energy=1,.entity="root"};
  expect(world.destruction3D->impact(world,physics,hit,affected,error),error);
  expect(world.destruction3D->cosmeticFragments().size()==4,"Accepted hit emitted no cosmetic chips");
  expect(world.entities.size()==entities && physics.overlapSphere(hit.position,hit.radius,{}, {},true).size()==overlaps,
         "Cosmetic chips created entities or colliders");
  hit.radius=-1;
  expect(!world.destruction3D->impact(world,physics,hit,affected,error),"Invalid impact accepted");
  expect(world.destruction3D->cosmeticFragments().size()==4,"Rejected impact emitted chips");
  physics.step(world,1.F/60,{0,0,0});
  expect(world.destruction3D->state("root").status=="applied","Decoration changed structural commit");
  world.destruction3D->updateCosmetics(world,4);
  expect(world.destruction3D->cosmeticFragments().empty(),"Cosmetic expiry failed");
}

void impactQueueGrowth() {
  auto world = fixture();
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  DestructionImpact3D impact{.position = {1.49F, 4, 0},
                             .radius = 0.06F,
                             .impulse = 1,
                             .direction = {0, 0, 1},
                             .entity = "root"};
  std::size_t affected;
  std::string error;
  for (int i = 0; i < 513; ++i)
    expect(world.destruction3D->impact(world, physics, impact, affected, error),
           error);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  expect(world.destruction3D->state("root").revision == 1 &&
             world.destruction3D->state("root").status == "applied",
         "Growing impulse queue discarded accepted work");
}

void manyImpactAssemblies() {
  auto world = fixture();
  for (int index = 1; index < 40; ++index) {
    auto source = fixture();
    const auto prefix = "copy_" + std::to_string(index) + "_";
    for (auto &entity : source.entities) {
      if (entity.id == "floor")
        continue;
      entity.id = prefix + entity.id;
      if (auto *transform = entity.component<Transform3DComponent>()) {
        if (transform->parent.empty())
          transform->position.x += index * 4.0F;
        else
          transform->parent = prefix + transform->parent;
      }
      if (auto *config = entity.component<Destructible3DComponent>())
        for (auto &[part, visual] : config->parts)
          visual = prefix + visual;
      world.entities.push_back(std::move(entity));
    }
  }
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  std::string error;
  std::size_t affected = 0;
  expect(world.destruction3D->impact(
             world, physics,
             {.position = {78, 4, 0}, .radius = 100, .energy = 1},
             affected, error), error);
  expect(affected == 40, "Impact silently dropped assemblies beyond 32");
  // The current scheduler commits one family per step, in stable ID order.
  for (int step = 0; step < 40; ++step)
    physics.step(world, 1.0F / 60, {0, 0, 0});
  for (int index = 0; index < 40; ++index) {
    const auto id = index == 0 ? "root" : "copy_" + std::to_string(index) + "_root";
    const auto state = world.destruction3D->state(id);
    expect(state.revision == 1 && state.status == "applied" && state.bodies == 1,
           "Multi-assembly impact lost work or multiplied damage: " + id);
  }
}

void sharedImpulseBudget() {
  auto world = fixture(false);
  // The generic moving fixture starts with spin; isolate the impulse budget
  // from inherited motion for this symmetric-contact assertion.
  auto *initial = findEntity(world, "root")->component<Rigidbody3DComponent>();
  initial->velocity = {};
  initial->angularVelocity = {};
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  DestructionImpact3D impact{
      .position = {0, 4, 2}, .radius = 3, .impulse = 9, .direction = {0, 0, 1}};
  expect(
      physics.overlapSphere(impact.position, impact.radius).size() == 1 &&
          physics.overlapSphere(impact.position, impact.radius, {}, {}, true)
                  .size() == 3,
      "Part-aware overlap changed legacy body deduplication or lost subshapes");
  std::size_t affected;
  std::string error;
  expect(world.destruction3D->impact(world, physics, impact, affected, error),
         error);
  physics.step(world, 1.0F / 60, {0, 0, 0});
  const auto *body =
      findEntity(world, "root")->component<Rigidbody3DComponent>();
  expect(std::abs(body->velocity.z - 3) < 0.02F &&
             std::abs(body->angularVelocity.y) < 0.01F,
         "Impulse budget was multiplied per part or symmetric contacts lost "
         "torque balance");
}
void localized() {
  auto world = fixture();
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F / 60);
  expect(world.destruction3D->state("root").status == "ready",
         "Attachment failed");
  hit(world, "right", 0.6F);
  expect(world.destruction3D->state("root").revision == 0,
         "Damage was not deferred");
  physics.step(world, 1.0F / 60);
  expect(world.destruction3D->state("root").bodies == 1,
         "Partial damage split prematurely");
  hit(world, "right", 0.6F);
  physics.step(world, 1.0F / 60);
  auto state = world.destruction3D->state("root");
  expect(state.status == "applied" && state.bodies == 2,
         "Split failed: " + state.error);
  const auto right = state.parts.at("right");
  expect(state.parts.at("left") == state.parts.at("middle") &&
             right != state.parts.at("left"),
         "Wrong connected groups");
  expect(findEntity(world, "visual_right")
                 ->component<Transform3DComponent>()
                 ->parent == right,
         "Visual not reparented");
  expect(findEntity(world, right)->component<Rigidbody3DComponent>()->mass == 1,
         "Split mass incorrect");
  const auto query = physics.raycast({1, 4, 3}, {0, 0, -1}, 6);
  expect(query && query->entityId == right && query->colliderPartId == "right",
         "Native split identity incorrect");
  for (int i = 0; i < 30; ++i)
    physics.step(world, 1.0F / 60);
  expect(
      findEntity(world, right)->component<Transform3DComponent>()->position.y <
          3,
      "Detached body did not fall under gravity");
  const auto remaining = physics.raycast({-1, 4, 3}, {0, 0, -1}, 6);
  expect(remaining && remaining->colliderPartId == "left",
         "Remaining anchored collision disappeared");
  hit(world, "middle", 2);
  physics.step(world, 1.0F / 60);
  state = world.destruction3D->state("root");
  expect(state.bodies == 3 && state.parts.at("right") == right,
         "Repeated split rebuilt unrelated ownership");
  for (int i = 0; i < 120; ++i)
    physics.step(world, 1.0F / 60);
  const auto resting =
      resolveWorldTransform3D(world, *findEntity(world, "visual_right"))
          ->position;
  expect(resting.y > 0.4F && resting.y < 0.7F,
         "Detached fragment did not collide with and settle on the floor");
  std::erase_if(world.entities,
                [](const Entity &entity) { return entity.id == "root"; });
  physics.step(world, 1.0F / 60);
  expect(world.destruction3D->state("root").status == "unattached",
         "Removed assembly retained damage state");
  for (const auto &[id, asset] : world.colliderAssets3D)
    expect(!id.starts_with("runtime-fracture://"),
           "Removed assembly leaked collider snapshots");
  expect(!physics.raycast({-1, 4, 3}, {0, 0, -1}, 6),
         "Removed fragments retained native collision");
}

void detachedFoundation() {
  auto world = fixture();
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F / 60);
  hit(world, "middle", 2);
  physics.step(world, 1.0F / 60);
  auto state = world.destruction3D->state("root");
  const auto base = state.parts.at("left");
  const auto unrelated = state.parts.at("right");
  expect(findEntity(world, base)->component<Rigidbody3DComponent>()->bodyType ==
             "static",
         "Foundation lost support from a neighboring hit");
  hit(world, "left", 0.6F);
  physics.step(world, 1.0F / 60);
  expect(findEntity(world, base)->component<Rigidbody3DComponent>()->bodyType ==
             "static",
         "Partial direct hit released foundation");
  hit(world, "left", 0.6F);
  physics.step(world, 1.0F / 60);
  state = world.destruction3D->state("root");
  expect(state.status == "applied",
         "Foundation release failed: " + state.error);
  expect(state.parts.at("right") == unrelated && state.bodies == 3,
         "Foundation release changed unrelated ownership");
  expect(findEntity(world, state.parts.at("left"))
                 ->component<Rigidbody3DComponent>()
                 ->bodyType == "dynamic",
         "Isolated foundation remained static");
  for (int i = 0; i < 30; ++i)
    physics.step(world, 1.0F / 60);
  expect(findEntity(world, state.parts.at("left"))
                 ->component<Transform3DComponent>()
                 ->position.y < 3,
         "Released foundation did not fall");
}
void motionAndPose(bool mixedDensity = false) {
  auto world = fixture(false);
  if (mixedDensity) {
    auto &parts = world.colliderAssets3D.at("asset://assembly").parts;
    parts[0].density = 100;
    parts[1].density = 200;
    parts[2].density = 300;
  }
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F / 60);
  const auto before =
      resolveWorldTransform3D(world, *findEntity(world, "visual_right"))
          ->position;
  hit(world, "middle", 2);
  physics.step(world, 0.000001F);
  const auto state = world.destruction3D->state("root");
  expect(state.bodies == 3, "Moving split failed: " + state.error);
  const auto after =
      resolveWorldTransform3D(world, *findEntity(world, "visual_right"))
          ->position;
  expect(std::abs(after.x - before.x) < 0.001F &&
             std::abs(after.y - before.y) < 0.001F,
         "Visual jumped at split boundary");
  Vec3 momentum{};
  for (const auto &[part, id] : state.parts) {
    const auto *body = findEntity(world, id)->component<Rigidbody3DComponent>();
    const float expectedMass = mixedDensity ? (part == "left" ? 0.5F : part == "right" ? 1.5F : 1.F) : 1.F;
    expect(std::abs(body->mass - expectedMass) < 0.001F &&
               std::abs(body->angularVelocity.z - 2) < 0.001F,
           "Mass/angular velocity inheritance failed");
    momentum.x += body->mass * body->velocity.x;
    momentum.y += body->mass * body->velocity.y;
  }
  expect(std::abs(momentum.x - 9) < 0.01F && std::abs(momentum.y) < 0.01F,
         "Linear momentum not conserved");
  Vec3 center{};
  for (const auto &[part, id] : state.parts) {
    const auto p =
        resolveWorldTransform3D(world, *findEntity(world, "visual_" + part))
            ->position;
    const auto mass = findEntity(world, id)->component<Rigidbody3DComponent>()->mass;
    center.x += p.x * mass / 3;
    center.y += p.y * mass / 3;
  }
  float angularMomentum = 0;
  for (const auto &[part, id] : state.parts) {
    const auto p =
        resolveWorldTransform3D(world, *findEntity(world, "visual_" + part))
            ->position;
    const auto *body = findEntity(world, id)->component<Rigidbody3DComponent>();
    // Three unit cubes: each local Izz=m/6; the initial compound Izz=2.5.
    angularMomentum += body->mass * (body->angularVelocity.z / 6 +
                       (p.x - center.x) * body->velocity.y -
                       (p.y - center.y) * body->velocity.x);
  }
  expect(std::abs(angularMomentum - (mixedDensity ? 13.F / 3 : 5.F)) < 0.02F,
         "Angular momentum not conserved");
  const auto *right = findEntity(world, state.parts.at("right"))
                          ->component<Rigidbody3DComponent>();
  expect(right->velocity.y > (mixedDensity ? 1.2F : 1.9F),
         "Split did not inherit rotational velocity at its COM");
}
void rollbackAndCancellation() {
  auto world = fixture();
  auto *config =
      findEntity(world, "root")->component<Destructible3DComponent>();
  config->maxBodies = 1;
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.0F / 60);
  hit(world, "right", 2);
  physics.step(world, 1.0F / 60);
  expect(world.destruction3D->state("root").status == "failed" &&
             world.destruction3D->state("root").revision == 0,
         "Budget rejection mutated committed damage");
  expect(physics.raycast({1, 4, 3}, {0, 0, -1}, 6)->entityId == "root",
         "Failure retired original collision");
  config->maxBodies = 64;
  hit(world, "right", 0.6F);
  physics.step(world, 1.0F / 60);
  expect(world.destruction3D->state("root").bodies == 1,
         "Cancelled damage leaked health");
  hit(world, "right", 2);
  // Remove and recreate the same authored ID before the queued hit executes.
  auto fresh = fixture();
  world.entities = std::move(fresh.entities);
  physics.step(world, 1.0F / 60);
  expect(world.destruction3D->state("root").revision == 0 &&
             world.destruction3D->state("root").bodies == 1,
         "Queued damage reached a recreated entity");
  std::string error;
  expect(!world.destruction3D->damagePart("root", "missing", 1, error),
         "Unknown part accepted");
  expect(!world.destruction3D->damagePart("root", "right", -1, error),
         "Negative damage accepted");
}
void invalidAttachments() {
  for (int invalid = 0; invalid < 3; ++invalid) {
    auto world = fixture();
    auto *root = findEntity(world, "root");
    if (invalid == 0)
      root->setComponent(BoxCollider3DComponent{});
    if (invalid == 1)
      findEntity(world, "visual_right")->persistent = true;
    if (invalid == 2)
      root->component<Destructible3DComponent>()->parts["right"] =
          "visual_left";
    ensurePhysicsWorld3D(world).step(world, 1.0F / 60);
    const auto state = world.destruction3D->state("root");
    expect(state.status == "failed" && !state.error.empty(),
           "Invalid runtime attachment was accepted");
    std::string error;
    expect(!world.destruction3D->damagePart("root", "right", 1, error),
           "Failed attachment accepted damage");
  }
}
void sceneLifecycle() {
  std::string error;
  auto loaded = loadProject(std::filesystem::path(DEMI_SOURCE_DIR) /
                                "examples/destruction_3d_lab/demi.project.json",
                            error);
  expect(bool(loaded), error);
  auto &world = loaded->world;
  const auto sceneId = world.activeSceneId;
  const auto authoredCount = world.entities.size();
  SceneFlow flow;
  flow.configure(loaded->project);
  ResourceLifetimeRegistry resources;
  resources.capture(sceneId, world.entities);
  const auto privateCount = [&] {
    std::size_t count = 0;
    for (const auto &[id, asset] : world.colliderAssets3D)
      if (id.starts_with("runtime-fracture://"))
        ++count;
    return count;
  };
  for (int cycle = 0; cycle < 4; ++cycle) {
    ensurePhysicsWorld3D(world).step(world, 1.0F / 60);
    expect(world.destruction3D->state("arch").bodies == 1 &&
               privateCount() == 0,
           "Reload retained old fracture state or private collider snapshots");
    expect(world.destruction3D->damagePart("arch", "lintel", 2, error), error);
    ensurePhysicsWorld3D(world).step(world, 1.0F / 60);
    expect(world.destruction3D->state("arch").bodies == 3 &&
               privateCount() == 3,
           "Repeated scene split failed");
    expect(flow.prepare(sceneId, false), "Scene reset preparation failed");
    for (int wait = 0;
         wait < 500 && flow.state() == ScenePreparationState::Loading; ++wait) {
      flow.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    expect(flow.state() == ScenePreparationState::Ready, flow.error());
    expect(flow.activate(world, resources).has_value(),
           "Scene reset activation failed");
    expect(world.entities.size() == authoredCount && !world.destruction3D,
           "Scene reset retained generated entities or damage owner");
  }
  ensurePhysicsWorld3D(world).step(world, 1.0F / 60);
  expect(world.destruction3D->damagePart("arch", "lintel", 2, error), error);
  ensurePhysicsWorld3D(world).step(world, 1.0F / 60);
  expect(flow.unload(world, sceneId, resources).has_value(),
         "Scene unload failed");
  ensurePhysicsWorld3D(world).step(world, 1.0F / 60);
  expect(world.entities.empty() && privateCount() == 0 &&
             world.destruction3D->state("arch").status == "unattached",
         "Scene unload leaked fragments, private geometry or damage state");
}
} // namespace
int main() {
  try {
    spatialImpacts();
    impactSupportAndRollback();
    checkpointsAndCleanup();
    repeatedSleepingFragmentHits();
    repeatedSleepingFragmentHits(true);
    impactStrengthAndFalloff();
    impactAcrossAssemblies();
    impactQueueGrowth();
    cosmeticImpactEffects();
    realFragmentFading();
    manyImpactAssemblies();
    sharedImpulseBudget();
    invalidAttachments();
    detachedFoundation();
    sceneLifecycle();
    for (int i = 0; i < 5; ++i) {
      localized();
    motionAndPose();
    motionAndPose(true);
      rollbackAndCancellation();
    }
    std::cout
        << "World destruction ownership, splits, motion and rollback passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
