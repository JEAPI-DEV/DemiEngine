#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace demi::runtime::render {

// Owned positions only: camera-independent nodes survive presentations without
// retaining component pointers. Geometry/wind expansion is supplied per query.
class InstanceSpatialIndex3D {
public:
  struct Node {
    Vec3 minimum;
    Vec3 maximum;
    float maximumScale = 0;
    std::size_t first = 0;
    std::size_t count = 0;
    std::size_t left = 0;
    std::size_t right = 0;
    bool leaf = true;
  };

  template <class Position, class Scale>
  void rebuild(std::size_t count, Position position, Scale scale) {
    nodes_.clear();
    order_.resize(count);
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    for (std::size_t index = 0; index < count; ++index) {
      const auto point = position(index);
      if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
          !std::isfinite(point.z) || !std::isfinite(scale(index))) {
        // Overflowing live transforms must not violate nth_element's ordering
        // requirements. Fall back to the existing exact per-copy path.
        return;
      }
    }
    if (count > 0) {
      build(0, count, position, scale);
    }
  }

  template <class Visible>
  std::size_t query(Visible visible, std::vector<std::size_t> &candidates,
                    std::vector<std::uint64_t> &visibilityWords) const {
    candidates.clear();
    visibilityWords.assign((order_.size() + 63U) / 64U, 0);
    std::size_t tested = 0;
    if (!nodes_.empty()) {
      visit(0, visible, visibilityWords, tested);
    } else if (!order_.empty()) {
      candidates = order_;
      return tested;
    }
    // Recover authored order without sorting thousands of accepted copies for
    // every camera and shadow cascade. The scratch mask is owned by the caller.
    for (std::size_t wordIndex = 0; wordIndex < visibilityWords.size();
         ++wordIndex) {
      auto word = visibilityWords[wordIndex];
      while (word != 0) {
        candidates.push_back(wordIndex * 64U + std::countr_zero(word));
        word &= word - 1U;
      }
    }
    return tested;
  }

private:
  template <class Position, class Scale>
  std::size_t build(std::size_t first, std::size_t count, Position &position,
                    Scale &scale) {
    Node node;
    node.first = first;
    node.count = count;
    node.minimum = node.maximum = position(order_[first]);
    for (std::size_t offset = first; offset < first + count; ++offset) {
      const auto point = position(order_[offset]);
      node.minimum = {std::min(node.minimum.x, point.x),
                      std::min(node.minimum.y, point.y),
                      std::min(node.minimum.z, point.z)};
      node.maximum = {std::max(node.maximum.x, point.x),
                      std::max(node.maximum.y, point.y),
                      std::max(node.maximum.z, point.z)};
      node.maximumScale = std::max(node.maximumScale, scale(order_[offset]));
    }
    const auto index = nodes_.size();
    nodes_.push_back(node);
    // This is an internal traversal granularity, not a population limit.
    constexpr std::size_t LeafCopies = 32;
    if (count > LeafCopies) {
      const Vec3 extent{node.maximum.x - node.minimum.x,
                        node.maximum.y - node.minimum.y,
                        node.maximum.z - node.minimum.z};
      const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0
                       : extent.y >= extent.z                       ? 1
                                                                    : 2;
      const auto coordinate = [axis](Vec3 point) {
        return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
      };
      const auto middle = first + count / 2;
      std::nth_element(order_.begin() + first, order_.begin() + middle,
                       order_.begin() + first + count,
                       [&](std::size_t left, std::size_t right) {
                         const auto a = coordinate(position(left));
                         const auto b = coordinate(position(right));
                         return a != b ? a < b : left < right;
                       });
      nodes_[index].leaf = false;
      const auto left = build(first, count / 2, position, scale);
      const auto right = build(middle, count - count / 2, position, scale);
      nodes_[index].left = left;
      nodes_[index].right = right;
    }
    return index;
  }

  template <class Visible>
  void visit(std::size_t index, Visible &visible,
             std::vector<std::uint64_t> &visibilityWords,
             std::size_t &tested) const {
    const auto &node = nodes_[index];
    ++tested;
    if (!visible(node)) {
      return;
    }
    if (node.leaf) {
      for (std::size_t offset = node.first; offset < node.first + node.count;
           ++offset) {
        const auto copy = order_[offset];
        visibilityWords[copy / 64U] |= std::uint64_t{1} << (copy % 64U);
      }
      return;
    }
    visit(node.left, visible, visibilityWords, tested);
    visit(node.right, visible, visibilityWords, tested);
  }
  std::vector<Node> nodes_;
  std::vector<std::size_t> order_;
};

} // namespace demi::runtime::render
