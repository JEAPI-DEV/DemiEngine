#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/WorldCommandBuffer.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <nlohmann/json.hpp>

#include <iostream>

using demi::runtime::RuntimeObjectModel;
using demi::runtime::World;
using demi::runtime::WorldCommandBuffer;

int main() {
  const std::string generatedTypes =
      demi::runtime::scene_loading::generatedLuaComponentTypes();
  if (generatedTypes.find("DemiTransform2DSpec") == std::string::npos ||
      generatedTypes.find("---@field parent? string") == std::string::npos) {
    std::cerr << "Lua component types were not generated from metadata.\n";
    return 1;
  }
  const auto *transformDescriptor =
      demi::runtime::scene_loading::findComponentDescriptor("Transform2D");
  if (transformDescriptor == nullptr ||
      transformDescriptor->fields.front().referenceKind !=
          demi::runtime::ComponentReferenceKind::Entity) {
    std::cerr << "Entity reference metadata was not retained.\n";
    return 1;
  }
  std::string error;
  auto parent = RuntimeObjectModel::buildEntity(
      nlohmann::json::parse(R"({
        "id": "parent",
        "tags": ["actor", "friendly"],
        "layer": "gameplay",
        "components": {
          "Transform2D": {"position": [2, 3]}
        }
      })"),
      error);
  if (!parent.has_value() || !parent->tags.contains("actor") ||
      parent->layer != "gameplay") {
    std::cerr << "Reflection-driven entity construction failed: " << error
              << '\n';
    return 1;
  }

  World world;
  if (!RuntimeObjectModel::addEntity(world, std::move(*parent)) ||
      RuntimeObjectModel::addEntity(
          world, *demi::runtime::findEntity(world, "parent"))) {
    std::cerr << "Duplicate entity IDs were not rejected.\n";
    return 1;
  }

  auto child = RuntimeObjectModel::buildEntity(
      nlohmann::json::parse(R"({
        "id": "child",
        "tags": ["actor"],
        "layer": "gameplay",
        "components": {
          "Transform2D": {"parent": "parent", "position": [1, 0]},
          "Sprite": {"shape": "rectangle", "size": [1, 1]}
        }
      })"),
      error);
  if (!child.has_value() ||
      !RuntimeObjectModel::addEntity(world, std::move(*child))) {
    std::cerr << "Child entity construction failed: " << error << '\n';
    return 1;
  }

  const auto worldPosition =
      RuntimeObjectModel::worldPosition(world, "child");
  if (!worldPosition.has_value() || (*worldPosition)[0] != 3.0 ||
      (*worldPosition)[1] != 3.0 ||
      RuntimeObjectModel::setParent(world, "parent", "child")) {
    std::cerr << "Hierarchy resolution or cycle rejection failed.\n";
    return 1;
  }

  const auto query = RuntimeObjectModel::query(
      world, {.allComponents = {"Transform2D"},
              .tags = {"actor"},
              .layer = "gameplay"});
  if (query.size() != 2) {
    std::cerr << "Entity component/tag/layer query failed.\n";
    return 1;
  }

  if (!RuntimeObjectModel::setComponentField(
          world, "child", "Sprite", "color",
          nlohmann::json::array({0.25, 0.5, 0.75, 1.0})) ||
      RuntimeObjectModel::componentField(world.entities[1], "Sprite",
                                         "color") !=
          nlohmann::json::array({0.25, 0.5, 0.75, 1.0}) ||
      RuntimeObjectModel::setComponentField(
          world, "child", "Sprite", "unknown", 1)) {
    std::cerr << "Generic component field access or validation failed.\n";
    return 1;
  }

  WorldCommandBuffer commands;
  if (!commands.setEnabled(world, "child", false) ||
      !commands.addComponent(world, "child", "CircleCollider2D",
                             {{"radius", 0.5}}) ||
      !demi::runtime::findEntity(world, "child")->enabled ||
      demi::runtime::findEntity(world, "child")
          ->hasComponent<demi::runtime::CircleCollider2DComponent>()) {
    std::cerr << "World commands were not deferred.\n";
    return 1;
  }
  const auto mutations = commands.flush(world);
  const auto *mutated = demi::runtime::findEntity(world, "child");
  if (mutations.size() != 2 || mutated == nullptr || mutated->enabled ||
      !RuntimeObjectModel::hasComponent(*mutated, "CircleCollider2D")) {
    std::cerr << "Deferred world commands did not flush in order.\n";
    return 1;
  }

  if (!commands.removeComponent(world, "child", "CircleCollider2D") ||
      !commands.destroy(world, "child")) {
    std::cerr << "Remove/destroy commands were rejected unexpectedly.\n";
    return 1;
  }
  (void)commands.flush(world);
  if (demi::runtime::findEntity(world, "child") != nullptr) {
    std::cerr << "Destroy command did not invalidate the stable entity ID.\n";
    return 1;
  }

  auto invalid = RuntimeObjectModel::buildEntity(
      nlohmann::json::parse(
          R"({"id":"bad","components":{"Transform2D":{"bogus":1}}})"),
      error);
  if (invalid.has_value()) {
    std::cerr << "Invalid runtime component fields bypassed validation.\n";
    return 1;
  }
  return 0;
}
