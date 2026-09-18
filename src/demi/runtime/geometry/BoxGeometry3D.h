#pragma once
#include "demi/runtime/scene/model/SceneTypes.h"
#include <array>
namespace demi::runtime::geometry {
// Shared unit-box surface convention for rendering and offline mesh tools.
inline constexpr std::array<Vec2, 4> boxFaceUvs{
    {{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
inline constexpr std::array<std::array<Vec3, 4>, 6> boxFaces{
    {{{{-.5F, -.5F, .5F}, {.5F, -.5F, .5F}, {.5F, .5F, .5F}, {-.5F, .5F, .5F}}},
     {{{.5F, -.5F, -.5F},
       {-.5F, -.5F, -.5F},
       {-.5F, .5F, -.5F},
       {.5F, .5F, -.5F}}},
     {{{-.5F, -.5F, -.5F},
       {-.5F, -.5F, .5F},
       {-.5F, .5F, .5F},
       {-.5F, .5F, -.5F}}},
     {{{.5F, -.5F, .5F}, {.5F, -.5F, -.5F}, {.5F, .5F, -.5F}, {.5F, .5F, .5F}}},
     {{{-.5F, .5F, .5F}, {.5F, .5F, .5F}, {.5F, .5F, -.5F}, {-.5F, .5F, -.5F}}},
     {{{-.5F, -.5F, -.5F},
       {.5F, -.5F, -.5F},
       {.5F, -.5F, .5F},
       {-.5F, -.5F, .5F}}}}};
} // namespace demi::runtime::geometry
