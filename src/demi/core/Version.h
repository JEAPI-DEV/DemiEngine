#pragma once

#include "demi/core/BuildVersion.h"

#include <string_view>

namespace demi {

inline constexpr std::string_view EngineName = "DemiEngine";
inline constexpr std::string_view EngineVersion = DEMI_ENGINE_VERSION;
inline constexpr std::string_view EngineBuildIteration = DEMI_BUILD_ITERATION;
inline constexpr std::string_view EngineBuildVersion =
    DEMI_ENGINE_VERSION "." DEMI_BUILD_ITERATION;
inline constexpr std::string_view EngineRepositoryUrl =
    "https://github.com/JEAPI-DEV/DemiEngine";
inline constexpr int CurrentFormatVersion = 1;

} // namespace demi
