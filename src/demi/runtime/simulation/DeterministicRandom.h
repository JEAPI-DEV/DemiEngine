#pragma once

#include <cstdint>

namespace demi::runtime::simulation {

class DeterministicRandom {
public:
  explicit DeterministicRandom(std::uint64_t seed = 1);

  void seed(std::uint64_t value);
  [[nodiscard]] std::uint64_t state() const;
  void restore(std::uint64_t value);
  [[nodiscard]] std::uint32_t nextU32();
  [[nodiscard]] float value();
  [[nodiscard]] float range(float minimum, float maximum);
  [[nodiscard]] int integer(int minimum, int maximum);

private:
  std::uint64_t state_ = 0;
};

} // namespace demi::runtime::simulation
