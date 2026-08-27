# Shared runtime sources and graphical/headless runtime library variants.
set(DEMI_RUNTIME_COMMON_SOURCES
  src/demi/runtime/animation/AnimationRuntime.cpp
  src/demi/runtime/animation/AnimationStateMachineSystem.cpp
  src/demi/runtime/animation/AnimationCollision2DSystem.cpp
  src/demi/runtime/animation/SpriteAnimationSystem.cpp
  src/demi/runtime/camera/Camera2DSystem.cpp
  src/demi/runtime/camera/Camera3DMath.cpp
  src/demi/runtime/camera/CameraRenderScheduler3D.cpp
  src/demi/runtime/data/DataAssetStore.cpp
  src/demi/runtime/tilemap/TilemapCollisionGenerator.cpp
  src/demi/runtime/tilemap/TilemapRuntime.cpp
  src/demi/runtime/app/RuntimeApp.cpp
  src/demi/runtime/app/EmbeddedRuntimeSession.cpp
  src/demi/runtime/app/ReloadCoordinator.cpp
  src/demi/runtime/audio/AudioClipPreprocess.cpp
  src/demi/runtime/audio/AudioMixer.cpp
  src/demi/runtime/audio/AudioSceneSystem.cpp
  src/demi/runtime/audio/AudioSystem.cpp
  src/demi/runtime/audio/MiniaudioAudioBackend.cpp
  src/demi/runtime/media/MediaSystem.cpp
  src/demi/runtime/network/DtlsTransport.cpp
  src/demi/runtime/network/GameNetworkSession.cpp
  src/demi/runtime/network/HttpClient.cpp
  src/demi/runtime/network/NetworkMessageGateway.cpp
  src/demi/runtime/network/NetworkFaultSimulator.cpp
  src/demi/runtime/network/NetworkOwnershipRegistry.cpp
  src/demi/runtime/network/NetworkSessionLifecycle.cpp
  src/demi/runtime/network/NetworkSystem.cpp
  src/demi/runtime/network/ReplicatedState.cpp
  src/demi/runtime/network/TlsMessaging.cpp
  src/demi/runtime/physics/Box2DWorldState.cpp
  src/demi/runtime/physics/Physics2D.cpp
  src/demi/runtime/physics/Physics3D.cpp
  src/demi/runtime/physics/PhysicsWorld3D.cpp
  src/demi/runtime/profiling/RuntimeProfiler.cpp
  src/demi/runtime/profiling/ProfilerHudLayout.cpp
  src/demi/runtime/platform/ProjectFileWatcher.cpp
  src/demi/runtime/simulation/DeterministicRandom.cpp
  src/demi/runtime/scene/HudParser.cpp
  src/demi/runtime/scene/ProjectParser.cpp
  src/demi/runtime/scene/SceneEntityParser.cpp
  src/demi/runtime/scene/SceneFlow.cpp
  src/demi/runtime/scene/SceneAssetReferences.cpp
  src/demi/runtime/scene/SceneLoader.cpp
  src/demi/runtime/assets/RegistryAssetResourceLoader.cpp
  src/demi/runtime/assets/RuntimeAssetBootstrap.cpp
  src/demi/runtime/assets/RuntimeAssetReload.cpp
  src/demi/runtime/assets/RuntimeAssetService.cpp
  src/demi/runtime/scripting/LuaScriptHostBindings.cpp
  src/demi/runtime/scripting/LuaScriptHost.cpp
  src/demi/runtime/scripting/LuaBindingCleanup.cpp
  src/demi/runtime/scripting/annotations/HandleActionAnnotation.cpp
  src/demi/runtime/scripting/annotations/LuaAnnotationScanner.cpp
  src/demi/runtime/scripting/annotations/LuaModulePath.cpp
  src/demi/runtime/scripting/annotations/OnEventAnnotation.cpp
  src/demi/runtime/scripting/LuaScriptDiagnostics.cpp
  src/demi/runtime/scripting/LuaScriptHostEntityServices.cpp
  src/demi/runtime/scripting/LuaScriptHostObjectModel.cpp
  src/demi/runtime/scripting/LuaScriptHostAnimationServices.cpp
  src/demi/runtime/scripting/LuaScriptHostPhysicsServices.cpp
  src/demi/runtime/scripting/LuaScriptHostHudServices.cpp
  src/demi/runtime/scripting/LuaScriptHostInputDebugServices.cpp
  src/demi/runtime/scripting/LuaScriptHostLoading.cpp
  src/demi/runtime/scripting/LuaScriptHostMediaNetworkServices.cpp
  src/demi/runtime/scripting/LuaScriptProperties.cpp
  src/demi/runtime/scripting/ScriptPropertyContract.cpp
  src/demi/runtime/scripting/ScriptComponentMetadata.cpp
  src/demi/runtime/scripting/bindings/LuaBindingHelpers.cpp
  src/demi/runtime/scripting/bindings/LuaCoreBindings.cpp
  src/demi/runtime/scripting/bindings/LuaEntityBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaPhysics2DBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaPhysics3DBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaRigidbody3DBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaCharacterController3DBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaCamera3DBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaRigidbody2DBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaSprite2DBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaTransform2DBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaTransform3DBindings.cpp
  src/demi/runtime/scripting/bindings/components/LuaTilemap2DBindings.cpp
  src/demi/runtime/scripting/bindings/data/LuaDataBindings.cpp
  src/demi/runtime/scripting/bindings/animation/LuaAnimationBindings.cpp
  src/demi/runtime/scripting/bindings/assets/LuaAssetsBindings.cpp
  src/demi/runtime/scripting/bindings/hud/LuaHudBindings.cpp
  src/demi/runtime/scripting/bindings/media/LuaAudioBindings.cpp
  src/demi/runtime/scripting/bindings/media/LuaCutsceneBindings.cpp
  src/demi/runtime/scripting/bindings/media/LuaVideoBindings.cpp
  src/demi/runtime/scripting/bindings/persistence/LuaSaveBindings.cpp
  src/demi/runtime/scripting/bindings/text/LuaRegexBindings.cpp
  src/demi/runtime/scripting/bindings/isometric/LuaIsoGridBindings.cpp
  src/demi/runtime/scripting/bindings/navigation/LuaNavigation2DBindings.cpp
  src/demi/runtime/scripting/text/RegexMatcher.cpp
  src/demi/runtime/scripting/bindings/LuaJsonBridge.cpp
  src/demi/runtime/scripting/bindings/LuaNetworkBindings.cpp
  src/demi/runtime/scripting/bindings/LuaNetworkSessionBindings.cpp
  src/demi/runtime/scripting/bindings/LuaRandomBindings.cpp
  src/demi/runtime/scripting/bindings/LuaTlsBindings.cpp
  src/demi/runtime/scripting/persistence/LuaSaveCodec.cpp
  src/demi/runtime/scripting/persistence/GameSaveDocument.cpp
  src/demi/runtime/scripting/LuaScriptHostPersistence.cpp
  src/demi/runtime/scripting/LuaScriptHostServices.cpp
  src/demi/runtime/scripting/LuaScriptHostSimulationServices.cpp
)

function(configure_demi_runtime target with_renderer)
  add_library(${target} STATIC ${DEMI_RUNTIME_COMMON_SOURCES} ${ARGN})
  target_link_libraries(${target} PUBLIC demi-core)
  target_include_directories(${target} PUBLIC src
    "${DEMI_RENDER_GENERATED_INCLUDE_DIR}")
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_link_libraries(${target} PRIVATE box2d Jolt miniaudio nlohmann_json::nlohmann_json mbedtls mbedx509 mbedcrypto sol2::sol2)
  target_compile_definitions(${target} PRIVATE
    DEMI_HAS_BOX2D=1
    DEMI_HAS_JOLT=1
    DEMI_HAS_MINIAUDIO=1
    DEMI_HAS_MBEDTLS=1
    DEMI_HAS_SOL2=1
    DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  if(${with_renderer})
    target_link_libraries(${target} PRIVATE SDL3::SDL3-static
      demi-render2d-bgfx demi-render3d-bgfx)
    target_compile_definitions(${target} PRIVATE DEMI_ENABLE_GRAPHICS_RUNTIME=1)
    if(TARGET PkgConfig::RSVG)
      target_link_libraries(${target} PRIVATE PkgConfig::RSVG)
      target_compile_definitions(${target} PRIVATE DEMI_HAS_RSVG=1)
    else()
      target_compile_definitions(${target} PRIVATE DEMI_HAS_RSVG=0)
    endif()
  else()
    target_compile_definitions(${target} PRIVATE
      DEMI_ENABLE_GRAPHICS_RUNTIME=0 DEMI_HAS_RSVG=0)
  endif()

  if(ANDROID)
    target_link_libraries(${target} PRIVATE lua54)
  elseif(NOT ${with_renderer})
    target_include_directories(${target} BEFORE PRIVATE
      ${CMAKE_SOURCE_DIR}/src/demi/runtime/scripting/lua_compat
      ${lua_SOURCE_DIR}
    )
    target_link_libraries(${target} PRIVATE demi-server-lua54 ${CMAKE_DL_LIBS})
  elseif(TARGET PkgConfig::LUA54)
    target_link_libraries(${target} PRIVATE PkgConfig::LUA54)
  else()
    message(FATAL_ERROR "Lua 5.4 was not found. Install lua5.4 development files to build DemiEngine scripting.")
  endif()

  if(DEMI_ENABLE_MEDIA)
    target_link_libraries(${target} PRIVATE PkgConfig::FFMPEG)
    target_compile_definitions(${target} PRIVATE DEMI_HAS_FFMPEG=1)
  else()
    target_compile_definitions(${target} PRIVATE DEMI_HAS_FFMPEG=0)
  endif()

  if(DEMI_ENABLE_NETWORK)
    target_link_libraries(${target} PRIVATE enet)
    target_include_directories(${target} PRIVATE ${enet_SOURCE_DIR}/include)
    target_compile_definitions(${target} PRIVATE DEMI_HAS_ENET=1)
  else()
    target_compile_definitions(${target} PRIVATE DEMI_HAS_ENET=0)
  endif()
endfunction()

configure_demi_runtime(demi-runtime-lib TRUE
  src/demi/runtime/app/BgfxAppContext.cpp
  src/demi/runtime/app/Bgfx2DAppHost.cpp
  src/demi/runtime/app/Bgfx3DAppHost.cpp
  src/demi/runtime/platform/PlatformInput.cpp
  src/demi/runtime/platform/SdlNativeWindow.cpp
  src/demi/runtime/platform/SdlPlatformHost.cpp
)

if(NOT ANDROID)
  configure_demi_runtime(demi-server-runtime-lib FALSE)
endif()
