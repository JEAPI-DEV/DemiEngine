#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace demi::runtime {
struct Destructible3DComponent {
  static constexpr std::string_view typeName = "Destructible3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"parts", ComponentFieldType::Object, false,
                               false},
      ComponentFieldDescriptor{"fading_parts", ComponentFieldType::Object,
                               false, false},
      ComponentFieldDescriptor{"deferred_visuals", ComponentFieldType::Object,
                               false, false},
      ComponentFieldDescriptor{"intact_visuals", ComponentFieldType::Object,
                               false, false},
      ComponentFieldDescriptor{"energy_per_health",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0.000001,
                               true,
                               false,
                               true,
                               true,
                               false,
                               1e12,
                               true},
      ComponentFieldDescriptor{"seed",
                               ComponentFieldType::Integer,
                               false,
                               true,
                               {},
                               0,
                               true,
                               false,
                               true,
                               true,
                               false,
                               4294967295.0,
                               true},
      ComponentFieldDescriptor{"generator_version",
                               ComponentFieldType::Integer,
                               false,
                               false,
                               {},
                               1,
                               true,
                               false,
                               true,
                               true,
                               false,
                               1,
                               true},
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
                                                  "Destructible 3D",
      "Add Fracture 3D to this mesh or its children. Foundation anchors determine the generated static/dynamic body type; mass and other Rigidbody settings are retained."};
  static void parse(const nlohmann::json &, Entity &);
  static nlohmann::json defaults();
  std::map<std::string, std::string> parts;
  std::map<std::string, std::pair<float,float>> fadingParts;
  // Cooked visual templates, shared across transactional world copies.
  std::shared_ptr<const nlohmann::json> deferredVisuals;
  std::map<std::string, std::vector<std::string>> intactVisuals;
  int maxBodies = 64;
  float energyPerHealth = 1000;
  std::uint32_t seed = 1;
  int generatorVersion = 1;
  std::uint64_t attachmentKey = 0; // Runtime ownership; never serialized.
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<Destructible3DComponent>::member<
          &Destructible3DComponent::parts>("parts"),
      RuntimeFieldBinding<Destructible3DComponent>::member<
          &Destructible3DComponent::fadingParts>("fading_parts"),
      RuntimeFieldBinding<Destructible3DComponent>::member<
          &Destructible3DComponent::deferredVisuals>("deferred_visuals"),
      RuntimeFieldBinding<Destructible3DComponent>::member<
          &Destructible3DComponent::intactVisuals>("intact_visuals"),
      RuntimeFieldBinding<Destructible3DComponent>::member<
          &Destructible3DComponent::energyPerHealth>("energy_per_health"),
      RuntimeFieldBinding<Destructible3DComponent>::member<
          &Destructible3DComponent::seed>("seed"),
      RuntimeFieldBinding<Destructible3DComponent>::member<
          &Destructible3DComponent::generatorVersion>("generator_version"),
      RuntimeFieldBinding<Destructible3DComponent>::member<
          &Destructible3DComponent::maxBodies>("max_bodies")};
};
} // namespace demi::runtime
