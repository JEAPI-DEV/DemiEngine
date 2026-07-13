#include "demi/runtime/simulation/DeterministicRandom.h"

#include <algorithm>

namespace demi::runtime::simulation {

DeterministicRandom::DeterministicRandom(const std::uint64_t seedValue) {
  seed(seedValue);
}

void DeterministicRandom::seed(const std::uint64_t value) {
  state_ = value == 0 ? 1 : value;
}

std::uint64_t DeterministicRandom::state() const { return state_; }

void DeterministicRandom::restore(const std::uint64_t value) {
  state_ = value == 0 ? 1 : value;
}

std::uint32_t DeterministicRandom::nextU32() {
  std::uint64_t value = state_;
  value ^= value >> 12U;
  value ^= value << 25U;
  value ^= value >> 27U;
  state_ = value;
  return static_cast<std::uint32_t>((value * 2685821657736338717ULL) >> 32U);
}

float DeterministicRandom::value() {
  constexpr float Scale = 1.0F / 4294967296.0F;
  return static_cast<float>(nextU32()) * Scale;
}

float DeterministicRandom::range(const float minimum, const float maximum) {
  const auto [low, high] = std::minmax(minimum, maximum);
  return low + (high - low) * value();
}

int DeterministicRandom::integer(const int minimum, const int maximum) {
  const auto [low, high] = std::minmax(minimum, maximum);
  const std::uint64_t span = static_cast<std::uint64_t>(
      static_cast<std::int64_t>(high) - static_cast<std::int64_t>(low) + 1);
  return low + static_cast<int>(static_cast<std::uint64_t>(nextU32()) % span);
}

} // namespace demi::runtime::simulation
