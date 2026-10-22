#pragma once

#include "demi/assets/GltfDocument.h"
#include "demi/assets/GltfMaterials.h"

namespace demi::assets {

// Private adapter shared by material-only loading and the complete model
// importer. cgltf ownership stays inside the asset integration.
[[nodiscard]] bool importGltfMaterials(const cgltf_data &document,
                                       const std::filesystem::path &source,
                                       std::vector<GltfMaterial3D> &materials,
                                       std::string &error);

} // namespace demi::assets
