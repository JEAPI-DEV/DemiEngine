# Android can continuously report SUBOPTIMAL for compositor-rotated landscape
# images. They remain usable; recreating the same swapchain every frame stalls
# the GPU without changing that advisory. Real resize/reset and surface errors
# still follow bgfx's existing recreation path.
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()
set(target_file "${SOURCE_DIR}/bgfx/src/renderer_vk.cpp")
file(READ "${target_file}" source)
if(source MATCHES "Demi Android suboptimal policy")
  return()
endif()
set(original "\t\t\tcase VK_ERROR_OUT_OF_DATE_KHR:\n\t\t\tcase VK_SUBOPTIMAL_KHR:")
# Promote the exact older cache-local fix to the maintained platform policy.
# Do not discard other edits in a dependency checkout.
set(legacy_acquire "\t\t\tcase VK_ERROR_OUT_OF_DATE_KHR:\n\t\t\t\tm_needToRecreateSwapchain = true;\n\t\t\t\treturn false;\n\n\t\t\tcase VK_SUBOPTIMAL_KHR:\n\t\t\t\t// The image was acquired and can still be presented per spec.\n\t\t\t\t// Displays whose transform differs from the requested preTransform\n\t\t\t\t// return this on every frame; recreating the swapchain each frame\n\t\t\t\t// destroys performance, so present the acquired image as-is.\n\t\t\t\tbreak;")
set(legacy_present "\t\t\tcase VK_ERROR_OUT_OF_DATE_KHR:\n\t\t\t\tm_needToRecreateSwapchain = true;\n\t\t\t\tbreak;\n\n\t\t\tcase VK_SUBOPTIMAL_KHR:\n\t\t\t\t// Presentable per spec; see the acquire path for why recreating\n\t\t\t\t// here would thrash the swapchain every frame.\n\t\t\t\tbreak;")
string(REPLACE "${legacy_acquire}" "${original}\n\t\t\t\tm_needToRecreateSwapchain = true;\n\t\t\t\treturn false;" source "${source}")
string(REPLACE "${legacy_present}" "${original}\n\t\t\t\tm_needToRecreateSwapchain = true;\n\t\t\t\tbreak;" source "${source}")
string(REGEX MATCHALL "case VK_ERROR_OUT_OF_DATE_KHR:\n\t\t\tcase VK_SUBOPTIMAL_KHR:" matches "${source}")
list(LENGTH matches count)
if(NOT count EQUAL 2)
  message(FATAL_ERROR "Expected exactly two pinned bgfx suboptimal handlers")
endif()
set(replacement "\t\t\tcase VK_SUBOPTIMAL_KHR:\n#if BX_PLATFORM_ANDROID\n\t\t\t{ // Demi Android suboptimal policy\n\t\t\t\tBGFX_PROFILER_SCOPE(\"vkSuboptimalUsable\", kColorFrame);\n\t\t\t\tbreak;\n\t\t\t}\n#endif\n\t\t\tcase VK_ERROR_OUT_OF_DATE_KHR:")
string(REPLACE "${original}" "${replacement}" source "${source}")
file(WRITE "${target_file}" "${source}")
