#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct AnimationLayerPlayback3D {
  std::string clipName;
  std::vector<std::string> mask;
  float weight = 0.0F;
  bool additive = false;
};

struct ProceduralBoneSegment3D {
  Vec3 start;
  Vec3 end;
  Vec3 pole;
};

struct AnimationPlayer3DComponent {
  static constexpr std::string_view typeName = "AnimationPlayer3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"clip_name", ComponentFieldType::String},
      ComponentFieldDescriptor{"speed", ComponentFieldType::Number},
      ComponentFieldDescriptor{"time", ComponentFieldType::Number},
      ComponentFieldDescriptor{"loop", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"playing", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Animation",
                                                  "Animation Player 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string clipName;
  float speed = 1.0F;
  float time = 0.0F;
  bool loop = true;
  bool playing = true;
  std::string previousClipName;
  float previousTime = 0.0F;
  float blendWeight = 1.0F;
  std::vector<AnimationLayerPlayback3D> layers;
  // Runtime-only world-space targets. Rendering converts them into model space
  // before evaluating the skin, so gameplay never handles inverse bind data.
  std::unordered_map<std::string, ProceduralBoneSegment3D> boneSegments;
  std::uint64_t proceduralPoseRevision = 0;
};

} // namespace demi::runtime
