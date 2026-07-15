#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct Box2DWorldState;

void releaseBox2DWorldState(Box2DWorldState *state);

struct Box2DWorldState {
  void *world = nullptr;
  std::unordered_map<std::string, void *> bodies;
  std::unordered_map<std::string, std::uint64_t> shapeSignatures;
  std::unordered_map<std::string, int> bodyTypes;
  std::vector<void *> joints;
  float gravityX = 0.0F;
  float gravityY = -18.0F;
  bool initialised = false;

  Box2DWorldState() = default;
  ~Box2DWorldState() { releaseBox2DWorldState(this); }
  Box2DWorldState(const Box2DWorldState &) = delete;
  Box2DWorldState &operator=(const Box2DWorldState &) = delete;
  Box2DWorldState(Box2DWorldState &&other) noexcept;
  Box2DWorldState &operator=(Box2DWorldState &&other) noexcept;
};

} // namespace demi::runtime
