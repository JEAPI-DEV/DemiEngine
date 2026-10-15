# Shader ablations are build diagnostics, never authored scene settings.
option(DEMI_PERFORMANCE_DEBUG "Enable diagnostic rendering ablations" OFF)
set(DEMI_PERFORMANCE_DEBUG_DISABLE "" CACHE STRING
  "Semicolon-separated diagnostic stages to disable when DEMI_PERFORMANCE_DEBUG is ON")

# Declaration order fixes the profiler bitmask contract. Append new stages;
# never reorder existing entries or reuse their bits.
set(DEMI_PERFORMANCE_DEBUG_STAGES
  terrain_materials
  terrain_normals
  terrain_macro
  terrain_triplanar
  terrain_blending
  shadow_receive)
set_property(CACHE DEMI_PERFORMANCE_DEBUG_DISABLE PROPERTY STRINGS
  ${DEMI_PERFORMANCE_DEBUG_STAGES})

set(DEMI_PERFORMANCE_DEBUG_VALUE 0)
set(DEMI_PERFORMANCE_DEBUG_MASK 0)
set(DEMI_PERFORMANCE_DEBUG_ACTIVE_STAGES "")
if(DEMI_PERFORMANCE_DEBUG)
  set(DEMI_PERFORMANCE_DEBUG_VALUE 1)
  foreach(stage IN LISTS DEMI_PERFORMANCE_DEBUG_DISABLE)
    if(NOT stage IN_LIST DEMI_PERFORMANCE_DEBUG_STAGES)
      message(FATAL_ERROR
        "Unknown DEMI_PERFORMANCE_DEBUG_DISABLE stage '${stage}'. Supported stages: ${DEMI_PERFORMANCE_DEBUG_STAGES}")
    endif()
  endforeach()
endif()

set(stage_bit 1)
foreach(stage IN LISTS DEMI_PERFORMANCE_DEBUG_STAGES)
  string(TOUPPER "${stage}" stage_macro)
  set(DEMI_PERFORMANCE_DEBUG_DISABLE_${stage_macro} 0)
  if(DEMI_PERFORMANCE_DEBUG AND stage IN_LIST DEMI_PERFORMANCE_DEBUG_DISABLE)
    set(DEMI_PERFORMANCE_DEBUG_DISABLE_${stage_macro} 1)
    math(EXPR DEMI_PERFORMANCE_DEBUG_MASK
      "${DEMI_PERFORMANCE_DEBUG_MASK} | ${stage_bit}")
    list(APPEND DEMI_PERFORMANCE_DEBUG_ACTIVE_STAGES "${stage}")
  endif()
  math(EXPR stage_bit "${stage_bit} * 2")
endforeach()
list(JOIN DEMI_PERFORMANCE_DEBUG_ACTIVE_STAGES "," DEMI_PERFORMANCE_DEBUG_DISABLED_NAMES)

set(DEMI_PERFORMANCE_DEBUG_GENERATED_DIR
  "${CMAKE_BINARY_DIR}/generated/demi/runtime/render")
file(MAKE_DIRECTORY "${DEMI_PERFORMANCE_DEBUG_GENERATED_DIR}")
set(DEMI_PERFORMANCE_DEBUG_SHADER_HEADER
  "${DEMI_PERFORMANCE_DEBUG_GENERATED_DIR}/demi_performance_debug.sh")
configure_file("${CMAKE_CURRENT_LIST_DIR}/PerformanceDebug.h.in"
  "${DEMI_PERFORMANCE_DEBUG_GENERATED_DIR}/PerformanceDebug.h" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/PerformanceDebug.h.in"
  "${DEMI_PERFORMANCE_DEBUG_SHADER_HEADER}" @ONLY)
