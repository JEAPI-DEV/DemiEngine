#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include <cstdint>
#include <map>
#include <string>

namespace demi::runtime {
struct Destructible3DComponent {
  static constexpr std::string_view typeName = "Destructible3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"parts", ComponentFieldType::Object, false, false},
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
                               256,
                               true}};
  static constexpr ComponentEditorMetadata editor{"Physics 3D",
                                                  "Destructible 3D"};
  static void parse(const nlohmann::json &, Entity &);
  static nlohmann::json defaults();
  std::map<std::string, std::string> parts;
  int maxBodies = 64;
  std::uint32_t seed = 1;
  int generatorVersion = 1;
  std::uint64_t attachmentKey = 0; // Runtime ownership; never serialized.
};
} // namespace demi::runtime
