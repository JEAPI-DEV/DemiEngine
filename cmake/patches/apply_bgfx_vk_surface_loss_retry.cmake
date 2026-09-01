# Applies cmake/patches/bgfx-vk-surface-loss-retry.patch to the vendored bgfx
# checkout. Invoked from the bgfx FetchContent PATCH_COMMAND with
# -DSOURCE_DIR=<bgfx source dir>. Idempotent: a patched tree is left untouched
# so re-running configure with an existing build directory is a no-op.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR (bgfx checkout) is required.")
endif()

set(patch_file "${CMAKE_CURRENT_LIST_DIR}/bgfx-vk-surface-loss-retry.patch")
set(target_file "${SOURCE_DIR}/bgfx/src/renderer_vk.cpp")

find_program(PATCH_EXECUTABLE patch)
if(NOT PATCH_EXECUTABLE)
  message(FATAL_ERROR "bgfx surface-loss patch requires the 'patch' tool.")
endif()

if(NOT EXISTS "${target_file}")
  message(FATAL_ERROR "bgfx renderer_vk.cpp not found at ${target_file}.")
endif()

file(READ "${target_file}" renderer_source)
if(renderer_source MATCHES "const VkResult swapChainResult = createSwapChain\\(\\);")
  message(STATUS "bgfx surface-loss patch already applied.")
  return()
endif()

execute_process(
  COMMAND "${PATCH_EXECUTABLE}" -p1 --forward "${patch_file}"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE patch_result
)
if(NOT patch_result EQUAL 0)
  message(FATAL_ERROR "Failed to apply bgfx surface-loss patch (${patch_result}).")
endif()
message(STATUS "Applied bgfx surface-loss patch.")