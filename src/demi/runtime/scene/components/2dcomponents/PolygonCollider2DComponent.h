#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace demi::runtime {

struct PolygonCollider2DComponent {
  static constexpr std::string_view typeName = "PolygonCollider2D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"points", ComponentFieldType::Vec2Array, true},
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
                                                  "Polygon Collider 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::vector<Vec2> points;
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
      RuntimeFieldBinding<PolygonCollider2DComponent>::member<
          &PolygonCollider2DComponent::points>("points"),
      RuntimeFieldBinding<PolygonCollider2DComponent>::member<
          &PolygonCollider2DComponent::offset>("offset"),
      RuntimeFieldBinding<PolygonCollider2DComponent>::member<
          &PolygonCollider2DComponent::isTrigger>("is_trigger"),
      RuntimeFieldBinding<PolygonCollider2DComponent>::member<
          &PolygonCollider2DComponent::layer>("layer"),
      RuntimeFieldBinding<PolygonCollider2DComponent>::member<
          &PolygonCollider2DComponent::categoryBits>("category_bits"),
      RuntimeFieldBinding<PolygonCollider2DComponent>::member<
          &PolygonCollider2DComponent::maskBits>("mask_bits"),
      RuntimeFieldBinding<PolygonCollider2DComponent>::member<
          &PolygonCollider2DComponent::friction>("friction"),
      RuntimeFieldBinding<PolygonCollider2DComponent>::member<
          &PolygonCollider2DComponent::restitution>("restitution"),
      RuntimeFieldBinding<PolygonCollider2DComponent>::member<
          &PolygonCollider2DComponent::density>("density"),
      RuntimeFieldBinding<PolygonCollider2DComponent>::member<
          &PolygonCollider2DComponent::debugVisible>("debug_visible")};
};

} // namespace demi::runtime
