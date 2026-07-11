#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace demi::runtime {

struct Camera3DComponent {
  Color clearColor;
  float fov = 60.0F;
  float orthographicSize = 10.0F;
  Vec3 targetOffset = {0.0F, 0.0F, 1.0F};
  bool perspective = true;
  float positionX = 0.0F;
  float upAxis = 1.0F;
};

struct MeshRendererComponent {
  std::string model;
  std::string shape = "cube";
  Vec3 size = {1.0F, 1.0F, 1.0F};
  Color color = {0.8F, 0.8F, 0.8F, 1.0F};
  std::string texture;
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  std::vector<Vec2> uvs;
  std::uint64_t revision = 0;
  Vec3 boundsMin;
  Vec3 boundsMax;
  bool hasBounds = false;
  bool wireframe = false;
};

struct AnimationPlayer3DComponent {
  int clip = 0;
  float speed = 1.0F;
  float time = 0.0F;
  bool loop = true;
  bool playing = true;
};

struct DirectionalLightComponent {
  Vec3 direction = {-0.4F, -1.0F, -0.3F};
  Color color = {1.0F, 1.0F, 0.95F, 1.0F};
  float intensity = 1.0F;
};

} // namespace demi::runtime
