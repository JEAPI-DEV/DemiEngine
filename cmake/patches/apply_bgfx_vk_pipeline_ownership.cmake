# Keep cached pipelines within the lifetime of their originating program/layout.
# Use bgfx's existing parent-aware cache eviction and deferred Vulkan release.
# Applies equally to graphics and compute, on every Vulkan platform.
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()
set(target_file "${SOURCE_DIR}/bgfx/src/renderer_vk.cpp")
file(READ "${target_file}" source)
function(replace_exact original replacement expected)
  string(REPLACE "${replacement}" "" probe "${source}")
  string(LENGTH "${source}" before)
  string(LENGTH "${probe}" after)
  string(LENGTH "${replacement}" width)
  math(EXPR expected_width "${width} * ${expected}")
  math(EXPR removed "${before} - ${after}")
  if(removed EQUAL expected_width)
    string(FIND "${source}" "${original}" unpatched)
    if(NOT unpatched EQUAL -1)
      message(FATAL_ERROR "Mixed patched/unpatched Vulkan pipeline ownership code")
    endif()
    return()
  endif()
  string(REPLACE "${original}" "" probe "${source}")
  string(LENGTH "${probe}" after)
  string(LENGTH "${original}" width)
  math(EXPR expected_width "${width} * ${expected}")
  math(EXPR removed "${before} - ${after}")
  if(NOT removed EQUAL expected_width)
    message(FATAL_ERROR "Pinned bgfx Vulkan pipeline ownership code changed; review the patch")
  endif()
  string(REPLACE "${original}" "${replacement}" source "${source}")
  set(source "${source}" PARENT_SCOPE)
endfunction()
replace_exact(
  "\t\tvoid destroyProgram(ProgramHandle _handle) override\n\t\t{\n\t\t\tm_program[_handle.idx].destroy();"
  "\t\tvoid destroyProgram(ProgramHandle _handle) override\n\t\t{\n\t\t\t// Demi program-owned Vulkan pipelines: retire before the layout.\n\t\t\tm_pipelineStateCache.invalidateWithParent(_handle.idx);\n\t\t\tm_program[_handle.idx].destroy();" 1)
replace_exact(
  "\t\t\tmurmur.begin();\n\t\t\tmurmur.add(program.m_vsh->m_hash);"
  "\t\t\tmurmur.begin();\n\t\t\tmurmur.add(_program.idx); // Demi compute pipeline owner\n\t\t\tmurmur.add(program.m_vsh->m_hash);" 1)
replace_exact(
  "\t\t\tmurmur.add(_stencil);\n\t\t\tmurmur.add(program.m_vsh->m_hash);"
  "\t\t\tmurmur.add(_stencil);\n\t\t\tmurmur.add(_program.idx); // Demi graphics pipeline owner\n\t\t\tmurmur.add(program.m_vsh->m_hash);" 1)
replace_exact("m_pipelineStateCache.add(hash, pipeline);"
              "m_pipelineStateCache.add(hash, pipeline, _program.idx);" 2)
file(READ "${target_file}" previous)
if(NOT source STREQUAL previous)
  file(WRITE "${target_file}" "${source}")
endif()
