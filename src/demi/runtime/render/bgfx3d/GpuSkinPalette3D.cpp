#include "demi/runtime/render/bgfx3d/GpuSkinPalette3D.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime::render {

bool packGpuSkinPalette(const GpuSkinPaletteLayout &layout,
                        const assets::GltfSkinnedModel3D::Pose &pose,
                        std::vector<float> &output, std::string &error) {
  output.clear();
  const auto reject = [&] {
    output.clear();
    error = "GPU skin palette has an invalid matrix reference or value.";
    return false;
  };
  if (layout.rows.empty() || layout.rows.size() > MaximumGpuSkinMatrices)
    return reject();
  constexpr std::array<float, 16> Identity{1, 0, 0, 0, 0, 1, 0, 0,
                                           0, 0, 1, 0, 0, 0, 0, 1};
  output.reserve(layout.rows.size() * 16);
  for (const auto &row : layout.rows) {
    const std::array<float, 16> *matrix = nullptr;
    switch (row.kind) {
    case GpuSkinPaletteLayout::Kind::SkinJoint:
      if (row.skin >= pose.skins.size() ||
          row.index >= pose.skins[row.skin].size())
        return reject();
      matrix = &pose.skins[row.skin][row.index];
      break;
    case GpuSkinPaletteLayout::Kind::Node:
      if (row.index >= pose.nodes.size())
        return reject();
      matrix = &pose.nodes[row.index];
      break;
    case GpuSkinPaletteLayout::Kind::Identity:
      matrix = &Identity;
      break;
    default:
      return reject();
    }
    if (!std::all_of(matrix->begin(), matrix->end(),
                     [](float v) { return std::isfinite(v); }))
      return reject();
    output.insert(output.end(), matrix->begin(), matrix->end());
  }
  error.clear();
  return true;
}

} // namespace demi::runtime::render
