#include "demi/runtime/physics/Box2DWorldState.h"

#include <box2d/box2d.h>

#include <utility>

namespace demi::runtime {

void releaseBox2DWorldState(Box2DWorldState *state) {
  if (state == nullptr)
    return;
  if (state->world != nullptr) {
    delete static_cast<b2World *>(state->world);
    state->world = nullptr;
  }
}

Box2DWorldState::Box2DWorldState(Box2DWorldState &&other) noexcept
    : world(other.world), bodies(std::move(other.bodies)),
      shapeSignatures(std::move(other.shapeSignatures)),
      bodyTypes(std::move(other.bodyTypes)), gravityX(other.gravityX),
      gravityY(other.gravityY), initialised(other.initialised) {
  other.world = nullptr;
  other.initialised = false;
}

Box2DWorldState &Box2DWorldState::operator=(Box2DWorldState &&other) noexcept {
  if (this != &other) {
    releaseBox2DWorldState(this);
    world = other.world;
    bodies = std::move(other.bodies);
    shapeSignatures = std::move(other.shapeSignatures);
    bodyTypes = std::move(other.bodyTypes);
    gravityX = other.gravityX;
    gravityY = other.gravityY;
    initialised = other.initialised;
    other.world = nullptr;
    other.initialised = false;
  }
  return *this;
}

} // namespace demi::runtime
