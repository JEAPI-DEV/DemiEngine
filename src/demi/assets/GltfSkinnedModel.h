#pragma once

#include "demi/assets/ModelImportProfile.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::assets {

struct GltfSkinnedVertex3D {
  runtime::Vec3 position;
  runtime::Vec2 uv;
  runtime::Color color{1.0F, 1.0F, 1.0F, 1.0F};
  std::array<std::uint16_t, 4> joints{};
  std::array<float, 4> weights{};
  int node = -1;
  int skin = -1;
};

struct GltfSkinnedModel3D {
  struct Node {
    std::array<float, 16> localMatrix{};
    runtime::Vec3 translation{};
    std::array<float, 4> rotation{0.0F, 0.0F, 0.0F, 1.0F};
    runtime::Vec3 scale{1.0F, 1.0F, 1.0F};
    int parent = -1;
    bool matrixAuthored = false;
  };

  struct Skin {
    std::vector<int> joints;
    std::vector<std::array<float, 16>> inverseBindMatrices;
  };

  enum class ChannelPath { Translation, Rotation, Scale };
  struct Channel {
    int node = -1;
    ChannelPath path = ChannelPath::Translation;
    std::vector<float> times;
    std::vector<std::array<float, 4>> values;
    bool step = false;
  };

  struct Clip {
    std::string name;
    float duration = 0.0F;
    std::vector<Channel> channels;
  };

  std::vector<GltfSkinnedVertex3D> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<Node> nodes;
  std::vector<Skin> skins;
  std::vector<Clip> clips;
  std::vector<std::byte> albedoImage;
  std::array<float, 16> importTransform{1.0F, 0.0F, 0.0F, 0.0F,
                                         0.0F, 1.0F, 0.0F, 0.0F,
                                         0.0F, 0.0F, 1.0F, 0.0F,
                                         0.0F, 0.0F, 0.0F, 1.0F};

  [[nodiscard]] int clipIndex(std::string_view name, int fallback = 0) const;
  [[nodiscard]] bool bindPosePositions(std::vector<runtime::Vec3> &out,
                                       std::string &error) const;
  [[nodiscard]] bool samplePositions(int clip, float time, bool loop,
                                     std::vector<runtime::Vec3> &out,
                                     std::string &error) const;
};

[[nodiscard]] std::optional<GltfSkinnedModel3D>
loadGltfSkinnedModel3D(const std::filesystem::path &path, std::string &error);
[[nodiscard]] std::optional<GltfSkinnedModel3D>
loadGltfSkinnedModel3D(const std::filesystem::path &path,
                       const ModelImportProfile &profile, std::string &error);

} // namespace demi::assets
