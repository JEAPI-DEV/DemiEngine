#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include <cstdint>
#include <map>
#include <string>
#include <memory>
#include <nlohmann/json.hpp>

namespace demi::runtime {
struct Destructible3DComponent {
  static constexpr std::string_view typeName = "Destructible3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"parts", ComponentFieldType::Object, false, false},
      ComponentFieldDescriptor{"deferred_visuals", ComponentFieldType::Object, false, false},
      ComponentFieldDescriptor{"energy_per_health", ComponentFieldType::Number, false, true, {}, 0.000001, true, false, true, true, false, 1e12, true},
      ComponentFieldDescriptor{"seed", ComponentFieldType::Integer, false, true, {}, 0, true, false, true, true, false, 4294967295.0, true},
      ComponentFieldDescriptor{"generator_version", ComponentFieldType::Integer, false, false, {}, 1, true, false, true, true, false, 1, true},
      ComponentFieldDescriptor{"max_bodies",
                               ComponentFieldType::Integer,
                               false,
                               true,
                               {},
                               1,
                               true,
                               false,
                               true,
                               true,
                               false,
                               0,
                               false}};
  static constexpr ComponentEditorMetadata editor{"Physics 3D",
                                                  "Destructible 3D"};
  static void parse(const nlohmann::json &, Entity &);
  static nlohmann::json defaults();
  std::map<std::string, std::string> parts;
  // Cooked visual templates, shared across transactional world copies.
  std::shared_ptr<const nlohmann::json> deferredVisuals;
  int maxBodies = 64;
  float energyPerHealth = 1000;
  std::uint32_t seed = 1;
  int generatorVersion = 1;
  std::uint64_t attachmentKey = 0; // Runtime ownership; never serialized.
};
} // namespace demi::runtime
