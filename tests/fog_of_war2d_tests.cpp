#include "demi/runtime/visibility/FogOfWar2D.h"
#include <array>
#include <cassert>
#include <cmath>
#include <limits>

using namespace demi::runtime;
int main() {
  FogOfWar2D fog;
  assert(!fog.configure(257, 10, 1, {}, {}, 0.5F));
  assert(fog.configure(24, 24, 1, {-12, -12}, {}, 0.45F));
  assert(fog.discovered().size() == 24 * 24);
  const auto discovery = fog.discoveryRevision();
  fog.reveal(3, 4);
  assert(fog.discovered()[4 * 24 + 3] == 1);
  assert(fog.discoveryRevision() != discovery);
  const auto remembered = fog.discoveryRevision();
  fog.reveal(3, 4);
  fog.reveal(-1, 4);
  assert(fog.discoveryRevision() == remembered);
  const std::array<float, 4> distances{6, 5, 6, 6};
  const std::array<float, 4> angles{0, 0.7F, 2.4F, 4.6F};
  assert(fog.update({2, 3}, 11, 1.4F, distances, angles));
  assert(fog.boundary().size() == 4);
  assert(fog.boundary()[0].point.x == 6);
  assert(fog.boundary()[0].point.y == 0);
  assert(std::abs(fog.boundary()[1].point.x - 5 * std::cos(0.7F)) < 0.00001F);
  assert(fog.focus().x == 2 && fog.focus().y == 3 && fog.radius() == 11);
  const auto revision = fog.revision();
  const std::array<float, 4> unordered{0, 2.4F, 0.7F, 4.6F};
  assert(!fog.update({}, 11, 1.4F, distances, unordered));
  auto invalid = distances;
  invalid[0] = std::numeric_limits<float>::quiet_NaN();
  assert(!fog.update({}, 11, 1.4F, invalid));
  assert(fog.revision() == revision && fog.focus().x == 2);
  assert(fog.update({3, 3}, 11, 1.4F, distances));
  assert(fog.discoveryRevision() == remembered);
  assert(fog.boundary().size() == 4);
  fog.clear();
  assert(fog.discovered().empty() && fog.boundary().empty());
  assert(fog.configure(24, 24, 1, {}, {}, 0.5F));
  assert(fog.revision() != revision);
}
