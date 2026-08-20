# Android runtime entry point and host development tools.
if(ANDROID)
  add_library(demi_android SHARED src/android/main.cpp)
  target_link_libraries(demi_android PRIVATE demi-runtime-lib SDL3::SDL3-static)
  target_link_libraries(demi_android PRIVATE android log OpenSLES EGL GLESv2)
else()
  find_package(CURL REQUIRED)
  add_library(demi-cli-support STATIC
    src/cli/doctor/DoctorService.cpp
    src/cli/project/ProjectDiscovery.cpp
    src/cli/project/ProjectTemplates.cpp
    src/cli/package/PackageCommands.cpp
    src/demi/runtime/scripting/PackageTestRunner.cpp
    src/demi/packages/PackageResolver.cpp
    src/demi/packages/PackageArchive.cpp
    src/demi/packages/PackageRegistry.cpp
    src/demi/packages/PackageInstaller.cpp)
  target_include_directories(demi-cli-support PUBLIC src PRIVATE ${lua_SOURCE_DIR})
  target_compile_features(demi-cli-support PUBLIC cxx_std_20)
  target_link_libraries(demi-cli-support PUBLIC demi-core CURL::libcurl
    mbedcrypto)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(demi-cli-support PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  add_executable(demi
    src/cli/AssetCommands.cpp
    src/cli/BuildCommands.cpp
    src/cli/CapabilityCommands.cpp
    src/cli/CookCommands.cpp
    src/cli/RuntimeCommands.cpp
    src/cli/SceneCompositionCommands.cpp
    src/cli/main.cpp
  )
  target_link_libraries(demi PRIVATE demi-core demi-cli-support demi-runtime-lib)
  target_compile_definitions(demi PRIVATE DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  if(NOT DEMI_HOST_SHADERC)
    add_dependencies(demi shaderc)
  endif()

  add_executable(demi-server
    src/cli/AssetCommands.cpp
    src/cli/BuildCommands.cpp
    src/cli/CapabilityCommands.cpp
    src/cli/CookCommands.cpp
    src/cli/RuntimeCommands.cpp
    src/cli/SceneCompositionCommands.cpp
    src/cli/main.cpp
  )
  target_link_libraries(demi-server PRIVATE demi-core demi-cli-support demi-server-runtime-lib)
  target_compile_definitions(demi-server PRIVATE
    DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    DEMI_SERVER_CLI=1)
  if(NOT DEMI_HOST_SHADERC)
    add_dependencies(demi-server shaderc)
  endif()

  add_executable(demi-runtime src/runtime/main.cpp)
  target_link_libraries(demi-runtime PRIVATE demi-core demi-runtime-lib)

  add_library(demi-editor-model STATIC
    src/editor/EditorDocumentStore.cpp
    src/editor/EditorPlaySession.cpp
    src/editor/EditorSceneCommand.cpp
    src/editor/EditorSceneDocument.cpp
    src/editor/EditorSceneJson.cpp
    src/editor/EditorSceneViewState.cpp
    src/editor/EditorViewportProjection.cpp
    src/editor/EditorViewportTool.cpp
    src/editor/EditorWorkspace.cpp)
  target_include_directories(demi-editor-model PUBLIC src)
  target_compile_features(demi-editor-model PUBLIC cxx_std_20)
  target_link_libraries(demi-editor-model PUBLIC demi-core PRIVATE
    demi-runtime-lib)

  add_library(demi-editor-ui STATIC
    src/editor/EditorConflictPanel.cpp
    src/editor/EditorHierarchyPanel.cpp
    src/editor/EditorInspectorPanel.cpp
    src/editor/EditorPanelStyle.cpp
    src/editor/EditorShell.cpp
    src/editor/EditorStbRectPack.cpp
    src/editor/EditorTheme.cpp
    src/editor/EditorUiHostBgfx.cpp
    "${bgfx_SOURCE_DIR}/bgfx/3rdparty/dear-imgui/imgui.cpp"
    "${bgfx_SOURCE_DIR}/bgfx/3rdparty/dear-imgui/imgui_draw.cpp"
    "${bgfx_SOURCE_DIR}/bgfx/3rdparty/dear-imgui/imgui_tables.cpp"
    "${bgfx_SOURCE_DIR}/bgfx/3rdparty/dear-imgui/imgui_widgets.cpp"
    "${bgfx_SOURCE_DIR}/bgfx/examples/common/imgui/imgui.cpp")
  target_include_directories(demi-editor-ui PUBLIC src PRIVATE
    "${bgfx_SOURCE_DIR}/bgfx/examples/common/imgui"
    "${bgfx_SOURCE_DIR}/bgfx/examples/common"
    "${bgfx_SOURCE_DIR}/bgfx/3rdparty"
    "${bgfx_SOURCE_DIR}/bx/include")
  target_compile_features(demi-editor-ui PUBLIC cxx_std_20)
  # The engine font atlas already owns stb_truetype's implementation. The bgfx
  # sample wrapper only needs its declarations when linked with runtime UI.
  target_compile_definitions(demi-editor-ui PRIVATE USE_LOCAL_STB=0)
  target_link_libraries(demi-editor-ui PUBLIC demi-editor-model PRIVATE
    demi-runtime-lib demi-graphics-bgfx SDL3::SDL3-static bgfx bx)

  add_executable(demi-editor src/editor/main.cpp)
  target_link_libraries(demi-editor PRIVATE demi-editor-ui)
endif()
