#pragma once

#include "demi/assets/GltfSkinnedModel.h"

namespace demi::runtime::render {

// Total matrix rows, matching vs_demi_skinned.sc, not a per-skin limit.
inline constexpr std::uint16_t MaximumGpuSkinMatrices = 128;

struct GpuSkinPaletteLayout {
  enum class Kind { SkinJoint, Node, Identity };
  struct Row {
    Kind kind = Kind::Identity;
    std::size_t skin = 0;
    std::size_t index = 0;
  };
  std::vector<Row> rows;
};

// Resolves the immutable model layout against the current pose. Does not call
// graphics APIs and can run on workers. Failed packing leaves output empty.
[[nodiscard]] bool
packGpuSkinPalette(const GpuSkinPaletteLayout &layout,
                   const assets::GltfSkinnedModel3D::Pose &pose,
                   std::vector<float> &output, std::string &error);

} // namespace demi::runtime::render
