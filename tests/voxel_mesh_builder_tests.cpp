#include "demi/runtime/geometry/VoxelMeshBuilder.h"
#include <iostream>
#include <limits>

int main() {
  demi::runtime::geometry::VoxelMeshWorld voxels(1, 1);
  const demi::runtime::geometry::VoxelTileMap tiles{{1, {0, 0, 0}}};
  voxels.setSection(0, 0, 0, {{0, 0, 0, 1}});
  if (voxels.buildSectionMesh(0, 0, 0, tiles, 1).vertexCount() != 36) {
    std::cerr << "Typed voxel mesh did not emit six exposed faces.\n";
    return 1;
  }
  voxels.setSection(1, 0, 0, {{0, 0, 0, 1}});
  if (voxels.buildSectionMesh(0, 0, 0, tiles, 1).vertexCount() != 30) {
    std::cerr << "Typed voxel mesh did not cull a neighboring section face.\n";
    return 1;
  }
  voxels.eraseSection(1, 0, 0);
  if (voxels.buildSectionMesh(0, 0, 0, tiles, 1).vertexCount() != 36) {
    std::cerr << "Unloading a voxel section did not expose its neighbor.\n";
    return 1;
  }
  using namespace demi::runtime::geometry;
  std::vector<VoxelColumn> columns(9);
  columns[4] = {0, 1, 1, 1, false};
  ProceduralMeshBuilder mesh;
  mesh.addVoxelHeightfield(columns, tiles, 1, 1, 0, 1, 1);
  if (mesh.vertexCount() != 30) {
    std::cerr << "Heightfield border did not produce top and side faces.\n";
    return 1;
  }
  try {
    (void)checkedVoxelCellCount(std::numeric_limits<int>::max(), 2, 2);
    std::cerr << "Overflowing voxel indices were accepted.\n";
    return 1;
  } catch (const std::invalid_argument &) {
  }
  std::cout << "Typed voxel mesh construction passed\n";
}
