#pragma once

#include <functional>

class b2FixtureDef;

namespace demi::runtime {
struct World;
struct Entity;
struct Transform2DComponent;

// The callback consumes each native shape before its local storage expires.
// Both simulation fixtures and authored queries use this shape construction.
void visitBox2DFixtures2D(
    const World &world, const Entity &entity,
    const Transform2DComponent &transform,
    const std::function<void(const b2FixtureDef &)> &visitor);
} // namespace demi::runtime
