#include "demi/runtime/render/bgfx3d/PrimitiveMeshFactory3D.h"

#include <array>
#include <cmath>
#include <numbers>

namespace demi::runtime::render {
namespace {

void createCube(PrimitiveMeshData3D &output) {
  constexpr std::array<Vec3, 4> Face{{{-0.5F, -0.5F, 0.5F},
                                      {0.5F, -0.5F, 0.5F},
                                      {0.5F, 0.5F, 0.5F},
                                      {-0.5F, 0.5F, 0.5F}}};
  constexpr std::array<Vec2, 4> FaceUvs{
      {{0.0F, 1.0F}, {1.0F, 1.0F}, {1.0F, 0.0F}, {0.0F, 0.0F}}};
  const std::array<std::array<Vec3, 4>, 6> faces{{
      Face,
      {{{0.5F, -0.5F, -0.5F}, {-0.5F, -0.5F, -0.5F},
        {-0.5F, 0.5F, -0.5F}, {0.5F, 0.5F, -0.5F}}},
      {{{-0.5F, -0.5F, -0.5F}, {-0.5F, -0.5F, 0.5F},
        {-0.5F, 0.5F, 0.5F}, {-0.5F, 0.5F, -0.5F}}},
      {{{0.5F, -0.5F, 0.5F}, {0.5F, -0.5F, -0.5F},
        {0.5F, 0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}}},
      {{{-0.5F, 0.5F, 0.5F}, {0.5F, 0.5F, 0.5F},
        {0.5F, 0.5F, -0.5F}, {-0.5F, 0.5F, -0.5F}}},
      {{{-0.5F, -0.5F, -0.5F}, {0.5F, -0.5F, -0.5F},
        {0.5F, -0.5F, 0.5F}, {-0.5F, -0.5F, 0.5F}}},
  }};
  for (const auto &face : faces) {
    const std::uint32_t base =
        static_cast<std::uint32_t>(output.positions.size());
    output.positions.insert(output.positions.end(), face.begin(), face.end());
    output.textureCoordinates.insert(output.textureCoordinates.end(),
                                     FaceUvs.begin(), FaceUvs.end());
    output.indices.insert(output.indices.end(),
                          {base, base + 1U, base + 2U, base, base + 2U,
                           base + 3U});
  }
}

void createSphere(PrimitiveMeshData3D &output) {
  constexpr std::uint32_t Slices = 16;
  constexpr std::uint32_t Stacks = 8;
  for (std::uint32_t stack = 0; stack <= Stacks; ++stack) {
    const float v = static_cast<float>(stack) / Stacks;
    const float latitude = v * std::numbers::pi_v<float>;
    for (std::uint32_t slice = 0; slice <= Slices; ++slice) {
      const float u = static_cast<float>(slice) / Slices;
      const float longitude = u * std::numbers::pi_v<float> * 2.0F;
      output.positions.push_back(
          {std::cos(longitude) * std::sin(latitude) * 0.5F,
           std::cos(latitude) * 0.5F,
           std::sin(longitude) * std::sin(latitude) * 0.5F});
      output.textureCoordinates.push_back({u, v});
    }
  }
  for (std::uint32_t stack = 0; stack < Stacks; ++stack)
    for (std::uint32_t slice = 0; slice < Slices; ++slice) {
      const std::uint32_t top = stack * (Slices + 1U) + slice;
      const std::uint32_t bottom = top + Slices + 1U;
      output.indices.insert(output.indices.end(),
                            {top, top + 1U, bottom, top + 1U, bottom + 1U,
                             bottom});
    }
}

void createCylinder(PrimitiveMeshData3D &output) {
  constexpr std::uint32_t Slices = 16;
  for (std::uint32_t slice = 0; slice <= Slices; ++slice) {
    const float u = static_cast<float>(slice) / Slices;
    const float angle = u * std::numbers::pi_v<float> * 2.0F;
    const float x = std::cos(angle) * 0.5F;
    const float z = std::sin(angle) * 0.5F;
    output.positions.insert(output.positions.end(),
                            {{x, -0.5F, z}, {x, 0.5F, z}});
    output.textureCoordinates.insert(output.textureCoordinates.end(),
                                     {{u, 1.0F}, {u, 0.0F}});
  }
  for (std::uint32_t slice = 0; slice < Slices; ++slice) {
    const std::uint32_t bottom = slice * 2U;
    output.indices.insert(output.indices.end(),
                          {bottom, bottom + 1U, bottom + 2U, bottom + 1U,
                           bottom + 3U, bottom + 2U});
  }

  const std::uint32_t bottomCenter =
      static_cast<std::uint32_t>(output.positions.size());
  output.positions.push_back({0.0F, -0.5F, 0.0F});
  output.textureCoordinates.push_back({0.5F, 0.5F});
  const std::uint32_t topCenter =
      static_cast<std::uint32_t>(output.positions.size());
  output.positions.push_back({0.0F, 0.5F, 0.0F});
  output.textureCoordinates.push_back({0.5F, 0.5F});
  for (std::uint32_t slice = 0; slice < Slices; ++slice) {
    const std::uint32_t bottom = slice * 2U;
    const std::uint32_t nextBottom = (slice + 1U) * 2U;
    output.indices.insert(output.indices.end(),
                          {bottomCenter, bottom, nextBottom,
                           topCenter, nextBottom + 1U, bottom + 1U});
  }
}

} // namespace

bool createPrimitiveMesh3D(const std::string_view shape,
                           PrimitiveMeshData3D &output) {
  output = {};
  if (shape == "plane") {
    output.positions = {{-0.5F, 0.0F, -0.5F}, {0.5F, 0.0F, -0.5F},
                        {0.5F, 0.0F, 0.5F}, {-0.5F, 0.0F, 0.5F}};
    output.textureCoordinates =
        {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
    output.indices = {0, 2, 1, 0, 3, 2};
  } else if (shape == "sphere") {
    createSphere(output);
  } else if (shape == "cylinder") {
    createCylinder(output);
  } else if (shape == "cube") {
    createCube(output);
  } else {
    return false;
  }
  return true;
}

} // namespace demi::runtime::render
