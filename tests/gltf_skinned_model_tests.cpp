#include "demi/assets/GltfSkinnedModel.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool finite(const demi::runtime::Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

float distanceSquared(const demi::runtime::Vec3 left,
                      const demi::runtime::Vec3 right) {
  const float x = left.x - right.x;
  const float y = left.y - right.y;
  const float z = left.z - right.z;
  return x * x + y * y + z * z;
}

float largestExtent(const std::vector<demi::runtime::Vec3> &positions) {
  assert(!positions.empty());
  demi::runtime::Vec3 minimum = positions.front();
  demi::runtime::Vec3 maximum = positions.front();
  for (const auto position : positions) {
    minimum.x = std::min(minimum.x, position.x);
    minimum.y = std::min(minimum.y, position.y);
    minimum.z = std::min(minimum.z, position.z);
    maximum.x = std::max(maximum.x, position.x);
    maximum.y = std::max(maximum.y, position.y);
    maximum.z = std::max(maximum.z, position.z);
  }
  return std::max({maximum.x - minimum.x, maximum.y - minimum.y,
                   maximum.z - minimum.z});
}

} // namespace

int main() {
  const std::filesystem::path asset = std::filesystem::path(DEMI_SOURCE_DIR) /
                                      "examples/animation_3d/assets" /
                                      "AnimationLib/UAL1_Standard.glb";
  std::string error;
  const auto model = demi::assets::loadGltfSkinnedModel3D(asset, error);
  assert(model && error.empty());
  assert(!model->vertices.empty());
  assert(!model->indices.empty());
  assert(!model->skins.empty());
  assert(!model->clips.empty());

  std::vector<demi::runtime::Vec3> bindPose;
  assert(model->bindPosePositions(bindPose, error));
  assert(bindPose.size() == model->vertices.size());
  for (const auto position : bindPose)
    assert(finite(position));

  for (const std::uint32_t index : model->indices)
    assert(index < model->vertices.size());
  for (const auto &skin : model->skins) {
    assert(skin.joints.size() == skin.inverseBindMatrices.size());
    for (const int joint : skin.joints)
      assert(joint >= 0 &&
             static_cast<std::size_t>(joint) < model->nodes.size());
  }

  const int firstClip = model->clipIndex({}, 0);
  assert(firstClip == 0);
  assert(model->clipIndex("definitely_missing", 999) ==
         static_cast<int>(model->clips.size() - 1U));
  const int namedClip = model->clipIndex(model->clips.front().name);
  assert(namedClip == 0);

  std::vector<demi::runtime::Vec3> start;
  std::vector<demi::runtime::Vec3> animated;
  int movingClip = -1;
  float sampleTime = 0.0F;
  bool moved = false;
  for (std::size_t clip = 0; clip < model->clips.size() && !moved; ++clip) {
    assert(model->samplePositions(static_cast<int>(clip), 0.0F, true, start,
                                  error));
    sampleTime = std::max(model->clips[clip].duration * 0.37F, 0.01F);
    assert(model->samplePositions(static_cast<int>(clip), sampleTime, true,
                                  animated, error));
    for (std::size_t index = 0; index < start.size(); ++index)
      moved =
          moved || distanceSquared(start[index], animated[index]) > 0.000001F;
    if (moved)
      movingClip = static_cast<int>(clip);
  }
  assert(error.empty());
  assert(start.size() == model->vertices.size());
  assert(animated.size() == start.size());
  for (std::size_t index = 0; index < start.size(); ++index) {
    assert(finite(start[index]));
    assert(finite(animated[index]));
  }
  assert(moved);
  assert(movingClip >= 0);

  std::vector<demi::runtime::Vec3> wrapped;
  assert(model->samplePositions(
      movingClip, model->clips[movingClip].duration * 1000.0F + sampleTime,
      true, wrapped, error));
  assert(wrapped.size() == animated.size());
  for (std::size_t index = 0; index < wrapped.size(); ++index)
    assert(distanceSquared(wrapped[index], animated[index]) < 0.001F);

  std::vector<demi::runtime::Vec3> clamped;
  assert(model->samplePositions(movingClip, -100.0F, false, clamped, error));
  assert(clamped.size() == start.size());
  for (std::size_t index = 0; index < clamped.size(); ++index)
    assert(distanceSquared(clamped[index], start[index]) < 0.000001F);

  assert(!model->samplePositions(-1, 0.0F, true, animated, error));
  assert(!error.empty());
  error.clear();
  assert(!demi::assets::loadGltfSkinnedModel3D(
      asset.parent_path() / "missing.glb", error));
  assert(!error.empty());

  // Quaternion keys q and -q describe the same orientation. Interpolation
  // must take the shortest hemisphere instead of collapsing through a zero
  // quaternion at the midpoint.
  demi::assets::GltfSkinnedModel3D hemisphere;
  hemisphere.nodes.push_back({});
  hemisphere.skins.push_back(
      {.joints = {0},
       .inverseBindMatrices = {{{1, 0, 0, 0, 0, 1, 0, 0,
                                  0, 0, 1, 0, 0, 0, 0, 1}}}});
  hemisphere.vertices.push_back(
      {.position = {1, 0, 0}, .joints = {0, 0, 0, 0},
       .weights = {1, 0, 0, 0}, .skin = 0});
  hemisphere.clips.push_back(
      {.name = "hemisphere",
       .duration = 1.0F,
       .channels = {{.node = 0,
                     .path = demi::assets::GltfSkinnedModel3D::ChannelPath::Rotation,
                     .times = {0.0F, 1.0F},
                     .values = {{{0.0F, 0.70710678F, 0.0F, 0.70710678F}},
                                {{0.0F, -0.70710678F, 0.0F, -0.70710678F}}}}}});
  std::vector<demi::runtime::Vec3> hemisphereStart;
  std::vector<demi::runtime::Vec3> hemisphereMiddle;
  assert(hemisphere.samplePositions(0, 0.0F, false, hemisphereStart, error));
  assert(hemisphere.samplePositions(0, 0.5F, false, hemisphereMiddle, error));
  assert(distanceSquared(hemisphereStart.front(), hemisphereMiddle.front()) <
         0.000001F);

  // Static models still need the entire owner-node hierarchy. Applying only
  // the immediate mesh node loses importer conversion and parent scaling.
  demi::assets::GltfSkinnedModel3D hierarchy;
  hierarchy.nodes.push_back(
      {.translation = {10.0F, 0.0F, 0.0F}, .scale = {2.0F, 2.0F, 2.0F}});
  hierarchy.nodes.push_back(
      {.translation = {1.0F, 0.0F, 0.0F}, .parent = 0});
  hierarchy.vertices.push_back(
      {.position = {1.0F, 0.0F, 0.0F}, .node = 1});
  assert(hierarchy.bindPosePositions(bindPose, error));
  assert(bindPose.size() == 1U);
  assert(distanceSquared(bindPose.front(), {14.0F, 0.0F, 0.0F}) <
         0.000001F);

  auto malformed = hierarchy;
  malformed.nodes[0].parent = 99;
  assert(!malformed.bindPosePositions(bindPose, error));
  assert(!error.empty());
  malformed = hierarchy;
  malformed.nodes[0].parent = 1;
  assert(!malformed.bindPosePositions(bindPose, error));
  assert(error.find("cycle") != std::string::npos);

  const std::array<float, 16> identityMatrix{
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  malformed = {};
  malformed.nodes.push_back({});
  malformed.skins.push_back({.joints = {0}});
  malformed.vertices.push_back(
      {.position = {}, .joints = {0, 0, 0, 0},
       .weights = {1, 0, 0, 0}, .skin = 0});
  assert(!malformed.bindPosePositions(bindPose, error));
  malformed.skins[0].inverseBindMatrices.push_back(identityMatrix);
  malformed.skins[0].joints[0] = 9;
  assert(!malformed.bindPosePositions(bindPose, error));
  malformed.skins[0].joints[0] = 0;
  malformed.vertices[0].joints[0] = 4;
  assert(!malformed.bindPosePositions(bindPose, error));

  // The hyena's glTF uses an ancestor 0.01 importer transform. Rendering raw
  // POSITION data made it about one hundred times too large and billboard-like.
  error.clear();
  const auto hyena = demi::assets::loadGltfSkinnedModel3D(
      std::filesystem::path(DEMI_SOURCE_DIR) /
          "examples/minimal_3d/assets/models/hyena/scene.gltf",
      error);
  assert(hyena && error.empty());
  assert(hyena->bindPosePositions(bindPose, error));
  const float hyenaExtent = largestExtent(bindPose);
  demi::runtime::Vec3 hyenaMinimum = bindPose.front();
  demi::runtime::Vec3 hyenaMaximum = bindPose.front();
  for (const auto position : bindPose) {
    hyenaMinimum.x = std::min(hyenaMinimum.x, position.x);
    hyenaMinimum.y = std::min(hyenaMinimum.y, position.y);
    hyenaMinimum.z = std::min(hyenaMinimum.z, position.z);
    hyenaMaximum.x = std::max(hyenaMaximum.x, position.x);
    hyenaMaximum.y = std::max(hyenaMaximum.y, position.y);
    hyenaMaximum.z = std::max(hyenaMaximum.z, position.z);
  }
  assert(hyenaExtent > 0.1F);
  assert(hyenaExtent < 10.0F);
  // glTF is Y-up. The animal's body is longer along Z than it is tall along
  // Y, so examples must not retain the old raylib-specific +90 degree tilt.
  assert(hyenaMaximum.z - hyenaMinimum.z >
         hyenaMaximum.y - hyenaMinimum.y);

  std::cout << "gltf skinned model tests passed\n";
  return 0;
}
