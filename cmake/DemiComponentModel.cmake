# Discover reflected components and generate their aggregate metadata.
set(DEMI_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${DEMI_GENERATED_INCLUDE_DIR}/demi/generated")
file(GLOB_RECURSE DEMI_COMPONENT_HEADERS CONFIGURE_DEPENDS
  RELATIVE "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/demi/runtime/scene/components/*Component.h"
)
file(GLOB_RECURSE DEMI_COMPONENT_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/demi/runtime/scene/components/*Component.cpp"
)
list(SORT DEMI_COMPONENT_HEADERS)
set(DEMI_COMPONENT_INCLUDES "#pragma once\n\n")
set(DEMI_COMPONENT_DESCRIPTORS "")
foreach(component_header IN LISTS DEMI_COMPONENT_HEADERS)
  get_filename_component(component_class "${component_header}" NAME_WE)
  string(APPEND DEMI_COMPONENT_INCLUDES
    "#include \"${component_header}\"\n")
  string(APPEND DEMI_COMPONENT_DESCRIPTORS
    "    makeComponentDescriptor<${component_class}>(),\n")
endforeach()
file(WRITE "${DEMI_GENERATED_INCLUDE_DIR}/demi/generated/ComponentIncludes.h"
  "${DEMI_COMPONENT_INCLUDES}")
file(WRITE "${DEMI_GENERATED_INCLUDE_DIR}/demi/generated/ComponentDescriptors.inc"
  "${DEMI_COMPONENT_DESCRIPTORS}")

set(DEMI_COMPONENT_MODEL_SOURCES
  src/demi/runtime/scene/ComponentRegistry.cpp
  src/demi/runtime/scene/RuntimeObjectModel.cpp
  src/demi/runtime/scene/RuntimePrefabService.cpp
  src/demi/runtime/scene/ResourceLifetimeRegistry.cpp
  src/demi/runtime/scene/SceneJson.cpp
  src/demi/runtime/scene/Transform3DHierarchy.cpp
  src/demi/runtime/scene/WorldCommandBuffer.cpp
  ${DEMI_COMPONENT_SOURCES}
)
