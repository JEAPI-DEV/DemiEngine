#include "demi/assets/GltfSkinnedModel.h"
#include "demi/assets/GltfGeometry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <iostream>
#include <ranges>
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

void staticGeometryUsesSelectedSceneAndAccessorLayout() {
  using Json = nlohmann::json;
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory = std::filesystem::temp_directory_path() /
                         ("demi-gltf-geometry-" + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  const auto source = directory / "mesh.gltf";
  const auto buffer = directory / "mesh.bin";
  // One leading word, then interleaved POSITION/UV records with an extra word.
  const float vertices[]{99.0F, 0.0F, 0.0F,  0.0F,  0.25F, 0.5F,  99.0F,
                         1.0F,  0.0F, 0.0F,  0.75F, 0.5F,  99.0F, 0.0F,
                         1.0F,  0.0F, 0.25F, 1.0F,  99.0F};
  Json document = {
      {"asset", {{"version", "2.0"}}},
      {"scene", 1},
      {"scenes", Json::array({{{"nodes", {2}}}, {{"nodes", {0}}}})},
      {"nodes", Json::array({{{"translation", {2, 3, 0}}, {"children", {1}}},
                             {{"mesh", 0}, {"scale", {-2, 1, 1}}},
                             {{"mesh", 0}, {"translation", {100, 0, 0}}}})},
      {"meshes",
       Json::array({{{"primitives",
                      Json::array({{{"attributes",
                                     {{"POSITION", 0}, {"TEXCOORD_0", 1}}},
                                    {"indices", 2}}})}}})},
      {"buffers", Json::array({{{"uri", "mesh.bin"}, {"byteLength", 0}}})},
      {"bufferViews", Json::array({{{"buffer", 0},
                                    {"byteOffset", 0},
                                    {"byteLength", sizeof(vertices)},
                                    {"byteStride", 6 * sizeof(float)}},
                                   {{"buffer", 0},
                                    {"byteOffset", sizeof(vertices)},
                                    {"byteLength", 0}}})},
      {"accessors", Json::array({{{"bufferView", 0},
                                  {"byteOffset", sizeof(float)},
                                  {"componentType", 5126},
                                  {"count", 3},
                                  {"type", "VEC3"},
                                  {"min", {0, 0, 0}},
                                  {"max", {1, 1, 0}}},
                                 {{"bufferView", 0},
                                  {"byteOffset", 4 * sizeof(float)},
                                  {"componentType", 5126},
                                  {"count", 3},
                                  {"type", "VEC2"}},
                                 {{"bufferView", 1},
                                  {"componentType", 5121},
                                  {"count", 3},
                                  {"type", "SCALAR"}}})}};

  const auto writeDocument = [&] {
    std::ofstream output(source);
    output << document.dump();
    assert(output.good());
  };
  const auto writeBuffer = [&](std::size_t indexWidth, bool invalidIndex) {
    std::vector<unsigned char> bytes(sizeof(vertices) + 3 * indexWidth);
    std::memcpy(bytes.data(), vertices, sizeof(vertices));
    for (std::size_t index = 0; index < 3; ++index) {
      bytes[sizeof(vertices) + index * indexWidth] =
          static_cast<unsigned char>(invalidIndex && index == 2 ? 9 : index);
    }
    std::ofstream output(buffer, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    assert(output.good());
    document["buffers"][0]["byteLength"] = bytes.size();
    document["bufferViews"][1]["byteLength"] = 3 * indexWidth;
  };
  std::string error;
  for (const std::size_t width : {1U, 2U, 4U}) {
    writeBuffer(width, false);
    document["accessors"][2]["componentType"] = width == 1   ? 5121
                                                : width == 2 ? 5123
                                                             : 5125;
    writeDocument();
    error.clear();
    const auto geometry = demi::assets::loadGltfGeometry(source, error);
    assert(geometry && error.empty());
    assert(geometry->triangles.size() == 1);
    const auto &triangle = geometry->triangles.front();
    assert(triangle.a.x == 2.0F && triangle.a.y == 3.0F);
    assert(triangle.b.x == 0.0F && triangle.b.y == 3.0F);
    assert(triangle.c.x == 2.0F && triangle.c.y == 4.0F);
    assert(triangle.uvA.x == 0.25F && triangle.uvA.y == 0.5F);
    assert(triangle.uvB.x == 0.75F && triangle.uvC.y == 1.0F);
    assert(geometry->minimum.x == 0.0F && geometry->maximum.x == 2.0F);
    const auto skinned = demi::assets::loadGltfSkinnedModel3D(source, error);
    assert(skinned && skinned->vertices.size() == 6);
  }

  writeBuffer(4, true);
  writeDocument();
  assert(!demi::assets::loadGltfGeometry(source, error));
  assert(!error.empty());

  writeBuffer(4, false);
  auto &primitive = document["meshes"][0]["primitives"][0];
  primitive.erase("indices");
  primitive["attributes"].erase("TEXCOORD_0");
  writeDocument();
  error.clear();
  const auto nonindexed = demi::assets::loadGltfGeometry(source, error);
  assert(nonindexed && nonindexed->triangles.size() == 1);
  assert(nonindexed->triangles[0].uvA.x == 0.0F);
  assert(nonindexed->triangles[0].uvA.y == 0.0F);

  demi::assets::ModelImportProfile profile;
  profile.metersPerUnit = 0.5F;
  const auto converted = demi::assets::loadGltfGeometry(source, profile, error);
  assert(converted && converted->triangles[0].a.x == 1.0F);
  assert(converted->triangles[0].c.y == 2.0F);

  // Exercise the same selected scene using an embedded GLB buffer rather than
  // relying on the separate animated GLB fixtures used below.
  Json embeddedDocument = document;
  embeddedDocument["buffers"][0].erase("uri");
  embeddedDocument["meshes"][0]["primitives"][0]["indices"] = 2;
  embeddedDocument["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"] = 1;
  std::string jsonChunk = embeddedDocument.dump();
  while (jsonChunk.size() % 4 != 0) {
    jsonChunk.push_back(' ');
  }
  std::ifstream binaryInput(buffer, std::ios::binary);
  std::vector<char> binaryChunk((std::istreambuf_iterator<char>(binaryInput)), {});
  while (binaryChunk.size() % 4 != 0) {
    binaryChunk.push_back(0);
  }
  const auto embeddedSource = directory / "mesh.glb";
  {
    std::ofstream output(embeddedSource, std::ios::binary);
    const auto writeWord = [&](std::uint32_t value) {
      for (unsigned int shift = 0; shift < 32; shift += 8) {
        output.put(static_cast<char>((value >> shift) & 0xffU));
      }
    };
    writeWord(0x46546c67U);
    writeWord(2);
    writeWord(static_cast<std::uint32_t>(28 + jsonChunk.size() + binaryChunk.size()));
    writeWord(static_cast<std::uint32_t>(jsonChunk.size()));
    writeWord(0x4e4f534aU);
    output.write(jsonChunk.data(), jsonChunk.size());
    writeWord(static_cast<std::uint32_t>(binaryChunk.size()));
    writeWord(0x004e4942U);
    output.write(binaryChunk.data(), binaryChunk.size());
    assert(output.good());
  }
  const auto embedded = demi::assets::loadGltfGeometry(embeddedSource, error);
  assert(embedded && embedded->triangles.size() == 1);
  assert(embedded->triangles[0].a.x == 2.0F);
  assert(embedded->triangles[0].b.x == 0.0F);
  assert(embedded->triangles[0].c.y == 4.0F);
  assert(embedded->triangles[0].uvB.x == 0.75F);

  std::filesystem::resize_file(buffer, sizeof(float));
  assert(!demi::assets::loadGltfGeometry(source, error));
  assert(!error.empty());
  std::filesystem::remove(buffer);
  assert(!demi::assets::loadGltfGeometry(source, error));
  std::filesystem::remove_all(directory);
}

} // namespace

int main() {
  staticGeometryUsesSelectedSceneAndAccessorLayout();
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

  demi::assets::GltfSkinnedModel3D procedural;
  procedural.nodes.push_back({.name = "leg_upper"});
  procedural.skins.push_back(
      {.joints = {0},
       .inverseBindMatrices = {
           {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}}});
  procedural.vertices.push_back({.position = {0, 1, 0},
                                 .joints = {0, 0, 0, 0},
                                 .weights = {1, 0, 0, 0},
                                 .skin = 0});
  const demi::assets::GltfSkinnedModel3D::BoneSegments proceduralPose{
      {"leg_upper", {.start = {2, 0, 0}, .end = {2, 1, 0}, .pole = {2, 0, 1}}}};
  assert(procedural.bindPosePositions(bindPose, error, proceduralPose));
  assert(distanceSquared(bindPose.front(), {2, 1, 0}) < 0.000001F);
  auto missingBone = proceduralPose;
  missingBone["missing"] = missingBone.at("leg_upper");
  assert(!procedural.bindPosePositions(bindPose, error, missingBone));
  assert(error.find("missing bone") != std::string::npos);

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

  error.clear();
  const auto spider = demi::assets::loadGltfSkinnedModel3D(
      std::filesystem::path(DEMI_SOURCE_DIR) /
          "examples/procedural_spider_3d/assets/procedural_spider/spider/"
          "spider_rig.glb",
      error);
  assert(spider && error.empty());
  assert(spider->clips.empty());
  assert(!spider->skins.empty());
  for (const std::string_view bone : {"body_root", "leg_1_l_upper",
                                      "leg_1_l_lower", "leg_4_r_upper",
                                      "leg_4_r_lower"})
    assert(std::ranges::find(spider->nodes, bone,
                             &demi::assets::GltfSkinnedModel3D::Node::name) !=
           spider->nodes.end());
  std::vector<demi::runtime::Vec3> spiderBind;
  std::vector<demi::runtime::Vec3> spiderPosed;
  assert(spider->bindPosePositions(spiderBind, error));
  assert(spider->bindPosePositions(
      spiderPosed, error,
      {{"leg_1_l_upper",
        {.start = {0, 0, 0}, .end = {0, 1, 0}, .pole = {1, 0, 0}}}}));
  assert(std::ranges::any_of(
      std::views::iota(std::size_t{0}, spiderBind.size()),
      [&](const std::size_t index) {
        return distanceSquared(spiderBind[index], spiderPosed[index]) >
               0.0001F;
      }));

  // Imported locomotion must remain in place when a character controller owns
  // translation. Raw Mixamo hip motion previously moved this visual several
  // metres away from its entity as soon as any gameplay animation started.
  error.clear();
  auto knightProfile =
      demi::assets::modelImportPreset("animated_character");
  knightProfile.sourceUp = "+z";
  knightProfile.sourceForward = "-y";
  const auto knight = demi::assets::loadGltfSkinnedModel3D(
      std::filesystem::path(DEMI_SOURCE_DIR) /
          "examples/souls_boss_arena/assets/models/cyberpunk_character/"
          "cyberpunk_character.glb",
      knightProfile, error);
  assert(knight && error.empty());
  std::vector<demi::runtime::Vec3> knightBind;
  assert(knight->bindPosePositions(knightBind, error));
  demi::runtime::Vec3 knightMinimum = knightBind.front();
  demi::runtime::Vec3 knightMaximum = knightBind.front();
  for (const auto position : knightBind) {
    knightMinimum.x = std::min(knightMinimum.x, position.x);
    knightMinimum.y = std::min(knightMinimum.y, position.y);
    knightMinimum.z = std::min(knightMinimum.z, position.z);
    knightMaximum.x = std::max(knightMaximum.x, position.x);
    knightMaximum.y = std::max(knightMaximum.y, position.y);
    knightMaximum.z = std::max(knightMaximum.z, position.z);
  }
  const float knightHeight = knightMaximum.y - knightMinimum.y;
  if (knightHeight <= knightMaximum.x - knightMinimum.x ||
      knightHeight <= knightMaximum.z - knightMinimum.z)
    std::cerr << "Cyberpunk character bind extents: "
              << knightMaximum.x - knightMinimum.x << ", " << knightHeight
              << ", " << knightMaximum.z - knightMinimum.z << '\n';
  assert(knightHeight > knightMaximum.x - knightMinimum.x);
  assert(knightHeight > knightMaximum.z - knightMinimum.z);
  for (const std::string_view clipName : {"Idle", "CombatRun", "DodgeRoll",
                                          "SwordAttack", "ShieldBlock"}) {
    const int clip = knight->clipIndex(clipName, -1);
    assert(clip >= 0);
    const float duration = knight->clips[static_cast<std::size_t>(clip)].duration;
    for (const float time : {0.0F, duration * 0.5F, duration}) {
      std::vector<demi::runtime::Vec3> pose;
      assert(knight->samplePositions(clip, time, false, pose, error));
      assert(largestExtent(pose) < 5.0F);
      assert(std::ranges::all_of(pose, [](const demi::runtime::Vec3 position) {
        return finite(position) && std::abs(position.x) < 5.0F &&
               std::abs(position.y) < 5.0F && std::abs(position.z) < 5.0F;
      }));
    }
  }

  std::cout << "gltf skinned model tests passed\n";
  return 0;
}
