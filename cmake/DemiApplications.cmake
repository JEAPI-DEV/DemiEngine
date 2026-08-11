# Android runtime entry point and host development tools.
if(ANDROID)
  add_library(demi_android SHARED src/android/main.cpp)
  target_link_libraries(demi_android PRIVATE demi-runtime-lib SDL3::SDL3-static)
  target_link_libraries(demi_android PRIVATE android log OpenSLES EGL GLESv2)
else()
  find_package(CURL REQUIRED)
  add_library(demi-cli-support STATIC
    src/cli/doctor/DoctorService.cpp
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
    src/cli/SceneCompositionCommands.cpp
    src/cli/main.cpp
  )
  target_link_libraries(demi-server PRIVATE demi-core demi-cli-support demi-server-runtime-lib)
  target_compile_definitions(demi-server PRIVATE DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  if(NOT DEMI_HOST_SHADERC)
    add_dependencies(demi-server shaderc)
  endif()

  add_executable(demi-runtime src/runtime/main.cpp)
  target_link_libraries(demi-runtime PRIVATE demi-core demi-runtime-lib)

  add_executable(demi-editor src/editor/main.cpp)
  target_link_libraries(demi-editor PRIVATE demi-core)
endif()
