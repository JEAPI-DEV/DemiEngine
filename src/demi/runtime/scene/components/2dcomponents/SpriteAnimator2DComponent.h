#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct SpriteAnimationEvent2D {
  int frame = 0;
  std::string name;
};

struct SpriteAnimationClip2D {
  int startFrame = 0;
  int frameCount = 1;
  float framesPerSecond = 10.0F;
  bool loop = true;
  std::vector<SpriteAnimationEvent2D> events;
};

struct SpriteAnimator2DComponent {
  static constexpr std::string_view typeName = "SpriteAnimator2D";
  static constexpr bool exposedToLua = true;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"frame_size", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"clips", ComponentFieldType::Object},
      ComponentFieldDescriptor{"clip", ComponentFieldType::String},
      ComponentFieldDescriptor{"speed", ComponentFieldType::Number},
      ComponentFieldDescriptor{"playing", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Animation",
                                                  "Sprite Animator 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  Vec2 frameSize{};
  std::unordered_map<std::string, SpriteAnimationClip2D> clips;
  std::string clip;
  float time = 0.0F;
  float speed = 1.0F;
  int currentFrame = 0;
  bool playing = true;
};

} // namespace demi::runtime
