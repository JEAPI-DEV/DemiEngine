#include "demi/runtime/scene/ComponentRegistry.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

int main() {
  try {
    using namespace demi::runtime;
    using namespace demi::runtime::scene_loading;
    Entity entity;
    const auto *transform = findComponentDescriptor("Transform3D");
    const auto *body = findComponentDescriptor("Rigidbody3D");
    transform->parse({{"position", {1, 2, 3}}}, entity);
    body->parse(nlohmann::json::object(), entity);
    const auto authored = entity.serializedComponents;
    entity.component<Transform3DComponent>()->position = {7, 8, 9};
    entity.component<Rigidbody3DComponent>()->velocity = {4, 5, 6};
    if (transform->inspect(entity).at("position") !=
            nlohmann::json({7, 8, 9}) ||
        body->inspect(entity).at("velocity") != nlohmann::json({4, 5, 6}) ||
        body->inspect(entity).at("body_type") != "dynamic" ||
        transform->inspect(entity).at("scale") != nlohmann::json({1, 1, 1}) ||
        entity.serializedComponents != authored ||
        transform->serialize(entity).at("position") !=
            nlohmann::json({1, 2, 3}))
      throw std::runtime_error("Runtime inspection must read native values "
                               "without changing authored serialization");
    entity.component<Transform3DComponent>()->position.x = 12;
    if (transform->inspect(entity).at("position")[0] != 12)
      throw std::runtime_error("Runtime inspection cached an earlier snapshot");
    Entity absent;
    if (!transform->inspect(absent).empty())
      throw std::runtime_error("Absent component produced inspection values");

    std::ifstream input(DEMI_SOURCE_DIR "/schemas/components.schema.json");
    if (!input) {
      std::cerr << "Could not read checked-in component schema.\n";
      return 1;
    }
    const auto checkedIn = nlohmann::json::parse(input);
    const auto generated =
        demi::runtime::scene_loading::canonicalComponentSchema();
    if (checkedIn != generated) {
      std::cerr << "Component schema differs from the runtime registry. "
                   "Regenerate it with demi schema export.\n";
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << "Component schema generation failed: " << error.what() << '\n';
    return 1;
  }
}
