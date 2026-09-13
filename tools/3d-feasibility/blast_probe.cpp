#include "BlastFixture.h"
#include <iostream>
#include <stdexcept>

int main() {
  try {
    for (int iteration = 0; iteration < 100; ++iteration) {
      if (runBlastFixture({.chunks = 6, .groupSize = 3}).groups !=
              std::vector<std::vector<std::uint32_t>>{{0, 1, 2}, {3, 4, 5}} ||
          runBlastFixture({.chunks = 9, .rowWidth = 3, .groupSize = 3}).groups !=
              std::vector<std::vector<std::uint32_t>>{{0, 1, 2}, {3, 4, 5}, {6, 7, 8}})
        throw std::runtime_error("selective bond damage must retain connected groups");
      if (splitBlastFixture(0.25F) !=
              std::vector<std::vector<std::uint32_t>>{{0, 1, 2}} ||
          splitBlastFixture(2.0F) !=
              std::vector<std::vector<std::uint32_t>>{{0}, {1}, {2}})
        throw std::runtime_error(
            "fracture threshold or chunk identity changed");
    }
    std::cout << "Blast CPU probe passed: 100 damage/split/cleanup cycles, "
                 "stable chunk IDs.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
