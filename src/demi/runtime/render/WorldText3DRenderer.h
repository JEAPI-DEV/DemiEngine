#pragma once

#include "demi/runtime/render/RenderStatistics.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <raylib.h>

#include <string>

namespace demi::runtime {

struct World;

void drawWorldText3D(const World &world, const ::Camera3D &camera,
                     Vec3 cameraPosition, const std::string &renderMask,
                     RenderStatistics &statistics);

} // namespace demi::runtime
