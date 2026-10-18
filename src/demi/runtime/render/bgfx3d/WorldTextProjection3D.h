#pragma once

#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/ui/UiModel.h"

namespace demi::runtime {
class EntityComponentQuery;
}

namespace demi::runtime::render {

[[nodiscard]] ui::UiDocument
projectWorldText3D(const World &world, const BgfxCameraFrame3D &frame,
                   EntityComponentQuery *query = nullptr);

} // namespace demi::runtime::render
