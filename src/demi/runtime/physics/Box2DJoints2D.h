#pragma once

#include <memory>
#include <span>
#include <string>
#include <unordered_map>

class b2Body;
class b2World;

namespace demi::runtime {
struct World;

// Owns native constraint identity and solver history. Bodies must notify this
// owner before destruction because Box2D otherwise destroys attached joints.
class Box2DJoints2D {
public:
  explicit Box2DJoints2D(b2World &world);
  ~Box2DJoints2D();
  void removeBody(const b2Body &body);
  void synchronize(const World &world,
                   std::span<const std::size_t> entityIndices,
                   const std::unordered_map<std::string, void *> &bodies);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace demi::runtime
