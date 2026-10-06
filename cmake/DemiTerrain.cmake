# Pure sampled-data and hydrology targets. Focused tests must not need the
# application, asset registry, physics integrations or renderer to compile.
add_library(demi-terrain-sampling STATIC
  src/demi/runtime/terrain/TerrainHeightField.cpp
  src/demi/runtime/terrain/TerrainSurface.cpp)
target_include_directories(demi-terrain-sampling PUBLIC src)
target_compile_features(demi-terrain-sampling PUBLIC cxx_std_20)

add_library(demi-terrain-water STATIC
  src/demi/runtime/terrain/TerrainWater.cpp
  src/demi/runtime/terrain/TerrainWaterSurfaceBuilder.cpp
  src/demi/runtime/terrain/TerrainWaterConnectivity.cpp
  src/demi/runtime/terrain/TerrainWaterAppearance.cpp
  src/demi/runtime/terrain/TerrainWaterQueries.cpp)
target_include_directories(demi-terrain-water PUBLIC src)
target_compile_features(demi-terrain-water PUBLIC cxx_std_20)
target_link_libraries(demi-terrain-water PUBLIC
  demi-terrain-sampling nlohmann_json::nlohmann_json)
