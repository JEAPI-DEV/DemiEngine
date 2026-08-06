#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>

namespace demi::runtime {

struct ParticleEmitter2DComponent {
  static constexpr std::string_view typeName = "ParticleEmitter2D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array<std::string_view, 3> shapes{"point", "box",
                                                          "circle"};
  static constexpr std::array fields{
      ComponentFieldDescriptor{"emission_shape", ComponentFieldType::String,
                               false, true, shapes},
      ComponentFieldDescriptor{"emission_size", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"rate", ComponentFieldType::Number, false, true,
                               {}, 0.0, true},
      ComponentFieldDescriptor{"burst", ComponentFieldType::Integer, false,
                               true, {}, 0.0, true},
      ComponentFieldDescriptor{"lifetime", ComponentFieldType::Number, false,
                               true, {}, 0.001, true},
      ComponentFieldDescriptor{"velocity_min", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"velocity_max", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"gravity", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"size_start", ComponentFieldType::Number, false,
                               true, {}, 0.0, true},
      ComponentFieldDescriptor{"size_end", ComponentFieldType::Number, false,
                               true, {}, 0.0, true},
      ComponentFieldDescriptor{"rotation_speed",
                               ComponentFieldType::Number},
      ComponentFieldDescriptor{"color_start", ComponentFieldType::Color},
      ComponentFieldDescriptor{"color_end", ComponentFieldType::Color},
      ComponentFieldDescriptor::assetReference("texture"),
      ComponentFieldDescriptor::assetReference("material"),
      ComponentFieldDescriptor{"sorting_order", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"seed", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"max_particles", ComponentFieldType::Integer,
                               false, true, {}, 1.0, true},
      ComponentFieldDescriptor{"mobile_max_particles",
                               ComponentFieldType::Integer, false, true, {},
                               1.0, true},
      ComponentFieldDescriptor{"playing", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"loop", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Effects",
                                                  "2D Particle Emitter"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string emissionShape = "point";
  Vec2 emissionSize{1.0F, 1.0F};
  float rate = 10.0F;
  int burst = 0;
  float lifetime = 1.0F;
  Vec2 velocityMin{-0.2F, 0.5F};
  Vec2 velocityMax{0.2F, 1.0F};
  Vec2 gravity{};
  float sizeStart = 0.15F;
  float sizeEnd = 0.0F;
  float rotationSpeed = 0.0F;
  Color colorStart{1.0F, 1.0F, 1.0F, 1.0F};
  Color colorEnd{1.0F, 1.0F, 1.0F, 0.0F};
  std::string texture;
  std::string material;
  int sortingOrder = 0;
  std::uint32_t seed = 1;
  int maxParticles = 256;
  int mobileMaxParticles = 96;
  bool playing = true;
  bool loop = true;
};

} // namespace demi::runtime
