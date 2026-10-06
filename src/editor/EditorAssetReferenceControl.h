#pragma once

#include "demi/assets/AssetRegistry.h"

#include <functional>
#include <string>

namespace demi::editor {

// The caller owns the field's required/optional policy and commits the result.
// Filtering uses the registry's manifest, including DataAsset content metadata.
using EditorAssetPredicate = std::function<bool(const AssetManifest &)>;

bool drawEditorAssetReferenceControl(const char *label,
                                     const AssetRegistry &registry,
                                     const EditorAssetPredicate &accepts,
                                     std::string &selected,
                                     bool allowNone = true);

} // namespace demi::editor
