#include "demi/runtime/render/backend/QuadBatch.h"

#include <algorithm>
#include <array>
#include <limits>

namespace demi::runtime::render {

QuadBatch::QuadBatch(const std::size_t maxQuadsPerDraw)
    : maxQuadsPerDraw_(
          std::min(maxQuadsPerDraw,
                   static_cast<std::size_t>(
                       std::numeric_limits<std::uint16_t>::max() / 4U))) {}

bool QuadBatch::add(const QuadBatchKey &key, const Quad2D &quad) {
  return addQuad(
      key,
      QuadCorners2D{
          .topLeft = {.x = quad.left,
                      .y = quad.top,
                      .u = quad.u0,
                      .v = quad.v0,
                      .rgba = quad.rgba},
          .topRight = {.x = quad.right,
                       .y = quad.top,
                       .u = quad.u1,
                       .v = quad.v0,
                       .rgba = quad.rgba},
          .bottomRight = {.x = quad.right,
                          .y = quad.bottom,
                          .u = quad.u1,
                          .v = quad.v1,
                          .rgba = quad.rgba},
          .bottomLeft = {.x = quad.left,
                         .y = quad.bottom,
                         .u = quad.u0,
                         .v = quad.v1,
                         .rgba = quad.rgba},
      });
}

bool QuadBatch::addQuad(const QuadBatchKey &key,
                        const QuadCorners2D &quad) {
  if (!prepareDraw(key, 4))
    return false;

  // Indices are local to each draw range so every batch remains valid for a
  // 16-bit index buffer regardless of the total frame vertex count.
  const auto base = static_cast<std::uint16_t>(currentDrawVertices_);
  const std::array<QuadVertex, 4> vertices{
      quad.topLeft, quad.topRight, quad.bottomRight, quad.bottomLeft};
  vertices_.insert(vertices_.end(), vertices.begin(), vertices.end());
  const std::array<std::uint16_t, 6> indices{
      base, static_cast<std::uint16_t>(base + 1U),
      static_cast<std::uint16_t>(base + 2U), base,
      static_cast<std::uint16_t>(base + 2U),
      static_cast<std::uint16_t>(base + 3U)};
  indices_.insert(indices_.end(), indices.begin(), indices.end());
  draws_.back().vertexCount += 4;
  draws_.back().indexCount += 6;
  currentDrawVertices_ += 4;
  ++quadCount_;
  return true;
}

bool QuadBatch::addTriangle(const QuadBatchKey &key,
                            const Triangle2D &triangle) {
  if (!prepareDraw(key, 3))
    return false;
  const auto base = static_cast<std::uint16_t>(currentDrawVertices_);
  const std::array<QuadVertex, 3> vertices{
      triangle.a, triangle.b, triangle.c};
  vertices_.insert(vertices_.end(), vertices.begin(), vertices.end());
  const std::array<std::uint16_t, 3> indices{
      base, static_cast<std::uint16_t>(base + 1U),
      static_cast<std::uint16_t>(base + 2U)};
  indices_.insert(indices_.end(), indices.begin(), indices.end());
  draws_.back().vertexCount += 3;
  draws_.back().indexCount += 3;
  currentDrawVertices_ += 3;
  return true;
}

bool QuadBatch::prepareDraw(const QuadBatchKey &key,
                            const std::size_t vertexCount) {
  const std::size_t maxVertices = maxQuadsPerDraw_ * 4U;
  if (maxVertices == 0 || vertexCount > maxVertices)
    return false;
  const bool needsDraw =
      draws_.empty() || !(draws_.back().key == key) ||
      currentDrawVertices_ + vertexCount > maxVertices;
  if (needsDraw) {
    draws_.push_back({.key = key,
                      .firstVertex =
                          static_cast<std::uint32_t>(vertices_.size()),
                      .vertexCount = 0,
                      .firstIndex =
                          static_cast<std::uint32_t>(indices_.size()),
                      .indexCount = 0});
    currentDrawVertices_ = 0;
  }
  return true;
}

void QuadBatch::clear() {
  currentDrawVertices_ = 0;
  quadCount_ = 0;
  vertices_.clear();
  indices_.clear();
  draws_.clear();
}

const std::vector<QuadVertex> &QuadBatch::vertices() const {
  return vertices_;
}

const std::vector<std::uint16_t> &QuadBatch::indices() const {
  return indices_;
}

const std::vector<QuadDrawRange> &QuadBatch::draws() const { return draws_; }

std::size_t QuadBatch::quadCount() const { return quadCount_; }

} // namespace demi::runtime::render
