#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string>

namespace demi::runtime {

struct CircleCollider2DComponent {
  static constexpr std::string_view typeName = "CircleCollider2D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"radius", ComponentFieldType::Number},
      ComponentFieldDescriptor{"offset", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"is_trigger", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"layer", ComponentFieldType::String},
      ComponentFieldDescriptor{"category_bits", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"mask_bits", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"friction", ComponentFieldType::Number},
      ComponentFieldDescriptor{"restitution", ComponentFieldType::Number},
      ComponentFieldDescriptor{"density", ComponentFieldType::Number},
      ComponentFieldDescriptor{"debug_visible", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Physics 2D",
                                                  "Circle Collider 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  float radius = 0.5F;
  Vec2 offset;
  bool isTrigger = false;
  std::string layer;
  std::uint16_t categoryBits = 1;
  std::uint16_t maskBits = 0xFFFF;
  float friction = 0.2F;
  float restitution = 0.0F;
  float density = 1.0F;
  bool debugVisible = true;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<CircleCollider2DComponent>::member<
          &CircleCollider2DComponent::radius>("radius"),
      RuntimeFieldBinding<CircleCollider2DComponent>::member<
          &CircleCollider2DComponent::offset>("offset"),
      RuntimeFieldBinding<CircleCollider2DComponent>::member<
          &CircleCollider2DComponent::isTrigger>("is_trigger"),
      RuntimeFieldBinding<CircleCollider2DComponent>::member<
          &CircleCollider2DComponent::layer>("layer"),
      RuntimeFieldBinding<CircleCollider2DComponent>::member<
          &CircleCollider2DComponent::categoryBits>("category_bits"),
      RuntimeFieldBinding<CircleCollider2DComponent>::member<
          &CircleCollider2DComponent::maskBits>("mask_bits"),
      RuntimeFieldBinding<CircleCollider2DComponent>::member<
          &CircleCollider2DComponent::friction>("friction"),
      RuntimeFieldBinding<CircleCollider2DComponent>::member<
          &CircleCollider2DComponent::restitution>("restitution"),
      RuntimeFieldBinding<CircleCollider2DComponent>::member<
          &CircleCollider2DComponent::density>("density"),
      RuntimeFieldBinding<CircleCollider2DComponent>::member<
          &CircleCollider2DComponent::debugVisible>("debug_visible")};
};

} // namespace demi::runtime
