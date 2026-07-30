#pragma once

#include "demi/runtime/render/backend/GpuResources.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace demi::runtime::render {

enum class BlendMode { Opaque, Alpha, Additive };

struct ScissorRect {
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;

  bool operator==(const ScissorRect &) const = default;
};

struct QuadBatchKey {
  TextureHandle texture;
  ProgramHandle program;
  BlendMode blend = BlendMode::Alpha;
  ScissorRect scissor;

  bool operator==(const QuadBatchKey &) const = default;
};

struct Quad2D {
  float left = 0.0F;
  float top = 0.0F;
  float right = 0.0F;
  float bottom = 0.0F;
  float u0 = 0.0F;
  float v0 = 0.0F;
  float u1 = 1.0F;
  float v1 = 1.0F;
  std::uint32_t rgba = 0xffffffffU;
};

struct QuadVertex {
  float x = 0.0F;
  float y = 0.0F;
  float u = 0.0F;
  float v = 0.0F;
  std::uint32_t rgba = 0xffffffffU;
};

struct Triangle2D {
  QuadVertex a;
  QuadVertex b;
  QuadVertex c;
};

struct QuadCorners2D {
  QuadVertex topLeft;
  QuadVertex topRight;
  QuadVertex bottomRight;
  QuadVertex bottomLeft;
};

struct QuadDrawRange {
  QuadBatchKey key;
  std::uint32_t firstVertex = 0;
  std::uint32_t vertexCount = 0;
  std::uint32_t firstIndex = 0;
  std::uint32_t indexCount = 0;
};

class QuadBatch {
public:
  explicit QuadBatch(std::size_t maxQuadsPerDraw = 16383);

  [[nodiscard]] bool add(const QuadBatchKey &key, const Quad2D &quad);
  [[nodiscard]] bool addQuad(const QuadBatchKey &key,
                             const QuadCorners2D &quad);
  [[nodiscard]] bool addTriangle(const QuadBatchKey &key,
                                 const Triangle2D &triangle);
  void clear();

  [[nodiscard]] const std::vector<QuadVertex> &vertices() const;
  [[nodiscard]] const std::vector<std::uint16_t> &indices() const;
  [[nodiscard]] const std::vector<QuadDrawRange> &draws() const;
  [[nodiscard]] std::size_t quadCount() const;

private:
  [[nodiscard]] bool prepareDraw(const QuadBatchKey &key,
                                 std::size_t vertexCount);

  std::size_t maxQuadsPerDraw_;
  std::size_t currentDrawVertices_ = 0;
  std::size_t quadCount_ = 0;
  std::vector<QuadVertex> vertices_;
  std::vector<std::uint16_t> indices_;
  std::vector<QuadDrawRange> draws_;
};

} // namespace demi::runtime::render
