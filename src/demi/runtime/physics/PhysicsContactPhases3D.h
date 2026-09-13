#pragma once
#include "demi/runtime/physics/Physics3DTypes.h"
#include <vector>

namespace demi::runtime {
// Current contacts must already be in deterministic dispatch order. Assigns
// pair-level enter/stay and appends exits in previous dispatch order.
// Pair-sorted input uses a linear merge; other orders use the general path.
void assignContactPhases3D(std::vector<PhysicsContact3D> &current,
                           const std::vector<PhysicsContact3D> &previous);
} // namespace demi::runtime
