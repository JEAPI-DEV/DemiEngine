#pragma once

#include "demi/assets/DataDocument.h"

namespace demi::assets {

// Parses YAML into the same immutable DataDocument contract used by JSON.
// Consumers remain independent from yaml-cpp types and localization policy.
[[nodiscard]] DataDocumentResult
parseYamlDataDocument(std::string_view text,
                      std::filesystem::path sourcePath = {},
                      const DataDocumentLimits &limits = {});

} // namespace demi::assets
