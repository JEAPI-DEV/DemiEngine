#pragma once

#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"

namespace demi::runtime::render {

inline const MeshRendererComponent &
visibleMeshGeometry3D(const VisibleMesh3D &draw) {
  return draw.terrainGeometry
             ? *draw.terrainGeometry
             : *draw.entity->component<MeshRendererComponent>();
}

inline const MeshMaterialSections3D *
visibleMeshSections3D(const VisibleMesh3D &draw) {
  return draw.terrainSections ? draw.terrainSections
         : draw.modelSections
             ? draw.modelSections
             : draw.entity->component<MeshMaterialSections3D>();
}

} // namespace demi::runtime::render
