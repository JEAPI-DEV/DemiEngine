#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"

#include <string>
#include <vector>

namespace demi::runtime {

struct AnimationLayerPlayback3D {
  int clip = -1;
  std::string clipName;
  std::vector<std::string> mask;
  float weight = 0.0F;
  bool additive = false;
};

struct AnimationPlayer3DComponent {
  static constexpr std::string_view typeName = "AnimationPlayer3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"clip", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"clip_name", ComponentFieldType::String},
      ComponentFieldDescriptor{"speed", ComponentFieldType::Number},
      ComponentFieldDescriptor{"time", ComponentFieldType::Number},
      ComponentFieldDescriptor{"loop", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"playing", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Animation",
                                                  "Animation Player 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  int clip = 0;
  std::string clipName;
  float speed = 1.0F;
  float time = 0.0F;
  bool loop = true;
  bool playing = true;
  int previousClip = -1;
  std::string previousClipName;
  float previousTime = 0.0F;
  float blendWeight = 1.0F;
  std::vector<AnimationLayerPlayback3D> layers;
};

} // namespace demi::runtime
