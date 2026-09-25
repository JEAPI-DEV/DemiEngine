#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
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
      ComponentFieldDescriptor{"atlas", ComponentFieldType::Object},
      ComponentFieldDescriptor{"clips", ComponentFieldType::Object},
      ComponentFieldDescriptor{"clip", ComponentFieldType::String},
      ComponentFieldDescriptor{"speed", ComponentFieldType::Number},
      ComponentFieldDescriptor{"playing", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Animation",
                                                  "Sprite Animator 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);
  static void copyClips(SpriteAnimator2DComponent &destination,
                        const SpriteAnimator2DComponent &source);
  static void afterRuntimeFieldChange(SpriteAnimator2DComponent &component,
                                     std::string_view field);

  Vec2 frameSize{};
  std::unordered_map<std::string, SpriteAnimationClip2D> clips;
  std::string clip;
  float time = 0.0F;
  float speed = 1.0F;
  int currentFrame = 0;
  bool playing = true;
  std::string previousClip;
  int previousFrame = 0;
  float blendWeight = 1.0F;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<SpriteAnimator2DComponent>::member<
          &SpriteAnimator2DComponent::frameSize>("frame_size"),
      RuntimeFieldBinding<SpriteAnimator2DComponent>{
          "atlas", copyClips,
          RuntimeFieldBinding<SpriteAnimator2DComponent>::member<
              &SpriteAnimator2DComponent::clips>("atlas").read}.withoutDefault(),
      RuntimeFieldBinding<SpriteAnimator2DComponent>{
          "clips", copyClips,
          RuntimeFieldBinding<SpriteAnimator2DComponent>::member<
              &SpriteAnimator2DComponent::clips>("clips").read},
      RuntimeFieldBinding<SpriteAnimator2DComponent>::member<
          &SpriteAnimator2DComponent::clip>("clip"),
      RuntimeFieldBinding<SpriteAnimator2DComponent>::member<
          &SpriteAnimator2DComponent::speed>("speed"),
      RuntimeFieldBinding<SpriteAnimator2DComponent>::member<
          &SpriteAnimator2DComponent::playing>("playing")};
};

} // namespace demi::runtime
