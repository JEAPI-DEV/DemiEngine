# Engine data, asset, scene, UI, input, physics, and navigation foundations.
add_library(demi-core STATIC
  src/demi/capabilities/CapabilityManifest.cpp
  src/demi/assets/AssetRegistry.cpp
  src/demi/assets/AssetHash.cpp
  src/demi/assets/DataAsset.cpp
  src/demi/assets/DataDocument.cpp
  src/demi/assets/YamlDataDocument.cpp
  src/demi/assets/ColliderAssetGenerator.cpp
  src/demi/assets/ModelImportProfile.cpp
  src/demi/assets/ModelInspector.cpp
  src/demi/assets/SceneBudget3D.cpp
  src/demi/assets/GltfGeometry.cpp
  src/demi/assets/GltfSkinnedModel.cpp
  src/demi/assets/AssetSourceFiles.cpp
  src/demi/assets/AssetImporter.cpp
  src/demi/assets/AssetImporterRegistry.cpp
  src/demi/assets/AssetCookGraph.cpp
  src/demi/assets/AssetGroup.cpp
  src/demi/assets/GeneratedAtlasCooker.cpp
  src/demi/assets/PackageContent.cpp
  src/demi/assets/AssetPackage.cpp
  src/demi/assets/AssetCooker.cpp
  src/demi/assets/RenderAsset.cpp
  src/demi/runtime/physics/ColliderAsset3D.cpp
  src/demi/runtime/render/ParticleSimulation3D.cpp
  src/demi/runtime/concurrency/JobSystem.cpp
  src/demi/diagnostics/Diagnostic.cpp
  src/demi/filesystem/ProjectDiscovery.cpp
  src/demi/filesystem/ProjectPaths.cpp
  src/demi/packages/SemanticVersion.cpp
  src/demi/runtime/scene/ProjectBuildSettings.cpp
  src/demi/runtime/scene/ProjectBuildValidation.cpp
  src/demi/packages/PackageManifest.cpp
  src/demi/packages/PackageHash.cpp
  src/demi/packages/PackageLock.cpp
  src/demi/schema/Validation.cpp
  src/demi/runtime/scene/composition/PrefabResolver.cpp
  src/demi/runtime/ui/UiLayoutEngine.cpp
  src/demi/runtime/ui/TextLayoutEngine.cpp
  src/demi/runtime/ui/TextShaper.cpp
  src/demi/runtime/ui/TextEditingEngine.cpp
  src/demi/runtime/ui/RichTextParser.cpp
  src/demi/runtime/ui/UiMutationQueue.cpp
  src/demi/runtime/ui/UiVirtualCollection.cpp
  src/demi/runtime/ui/UiTweenSystem.cpp
  src/demi/runtime/ui/UiVariables.cpp
  src/demi/runtime/ui/UiLocalization.cpp
  src/demi/runtime/ui/UiAccessibilityTree.cpp
  src/demi/runtime/ui/UiAccessibilityActions.cpp
  src/demi/runtime/ui/UiAccessibilityBridge.cpp
  src/demi/runtime/ui/UiEvent.cpp
  src/demi/runtime/ui/UiEventQueue.cpp
  src/demi/runtime/ui/UiInteractionController.cpp
  src/demi/runtime/ui/UiDocumentParser.cpp
  src/demi/runtime/ui/UiPrefabResolver.cpp
  src/demi/runtime/ui/UiPresentation.cpp
  src/demi/runtime/ui/UiStateController.cpp
  src/demi/runtime/ui/UiActionController.cpp
  src/demi/runtime/input/InputActionResolver.cpp
  src/demi/runtime/input/InputRebinding.cpp
  src/demi/runtime/input/TouchGestureRecognizer.cpp
  src/demi/runtime/platform/ApplicationServices.cpp
  src/demi/runtime/platform/ApplicationPermissions.cpp
  src/demi/runtime/input/replay/InputReplay.cpp
  src/demi/runtime/input/InputActionParser.cpp
  src/demi/runtime/isometric/GridTypes.cpp
  src/demi/runtime/isometric/IsoGridMath.cpp
  src/demi/runtime/isometric/GridOccupancy.cpp
  src/demi/runtime/isometric/GridPathfinder.cpp
  src/demi/runtime/isometric/PlacementRules.cpp
  src/demi/runtime/isometric/IsoWorldQueries.cpp
  src/demi/runtime/isometric/IsoGridApi.cpp
  src/demi/runtime/navigation/NavigationGrid2D.cpp
  src/demi/runtime/network/NetworkContract.cpp
  src/demi/runtime/tilemap/TilemapAsset.cpp
  src/demi/runtime/physics/Box2DWorldState.cpp
  src/demi/runtime/physics/SpatialQuery3D.cpp
  ${DEMI_COMPONENT_MODEL_SOURCES}
)

target_include_directories(demi-core PUBLIC src "${DEMI_GENERATED_INCLUDE_DIR}")
target_include_directories(demi-core PRIVATE
  "${bgfx_SOURCE_DIR}/bgfx/3rdparty/cgltf"
  "${bgfx_SOURCE_DIR}/bgfx/3rdparty/stb")

target_compile_features(demi-core PUBLIC cxx_std_20)
target_link_libraries(demi-core PUBLIC nlohmann_json::nlohmann_json box2d
  utf8proc harfbuzz SheenBidi::SheenBidi mbedcrypto bimg_decode bimg
  yaml-cpp::yaml-cpp)
set(DEMI_HOST_SHADERC "" CACHE FILEPATH
  "Existing host shaderc executable to reuse instead of building shaderc")
if(NOT ANDROID)
  if(DEMI_HOST_SHADERC)
    target_compile_definitions(demi-core PRIVATE
      DEMI_SHADERC_PATH="${DEMI_HOST_SHADERC}")
  else()
    target_compile_definitions(demi-core PRIVATE
      DEMI_SHADERC_PATH="$<TARGET_FILE:shaderc>")
  endif()
  target_compile_definitions(demi-core PRIVATE
    DEMI_BGFX_SHADER_INCLUDE_DIR="${bgfx_SOURCE_DIR}/bgfx/src")
endif()
