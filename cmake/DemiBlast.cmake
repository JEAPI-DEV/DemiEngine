include_guard(GLOBAL)
include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
# Only the audited CPU low-level subset; no PhysX or CUDA dependency.
FetchContent_Declare(demi_blast_source
  URL https://codeload.github.com/NVIDIA-Omniverse/PhysX/tar.gz/4f2103c3a9052906296defb12166753450ef787c
  URL_HASH SHA256=5bf5e9cbe4cc144c80252bd3eb126308e27e0105e4a4af096d0da5daf49d0618
  SOURCE_SUBDIR demi-unused-subdirectory)
FetchContent_MakeAvailable(demi_blast_source)
set(blast_root "${demi_blast_source_SOURCE_DIR}/blast")
add_library(demi-blast-lowlevel STATIC
  ${blast_root}/source/sdk/lowlevel/NvBlastActor.cpp
  ${blast_root}/source/sdk/lowlevel/NvBlastActorSerializationBlock.cpp
  ${blast_root}/source/sdk/lowlevel/NvBlastAsset.cpp
  ${blast_root}/source/sdk/lowlevel/NvBlastAssetHelper.cpp
  ${blast_root}/source/sdk/lowlevel/NvBlastFamily.cpp
  ${blast_root}/source/sdk/lowlevel/NvBlastFamilyGraph.cpp
  ${blast_root}/source/sdk/common/NvBlastAssert.cpp
  ${blast_root}/source/sdk/common/NvBlastAtomic.cpp
  ${blast_root}/source/sdk/common/NvBlastTime.cpp
  ${blast_root}/source/sdk/common/NvBlastTimers.cpp)
target_include_directories(demi-blast-lowlevel PUBLIC
  ${blast_root}/include/lowlevel
  ${blast_root}/include/shared/NvFoundation
  PRIVATE ${blast_root}/source/sdk/common)
target_compile_features(demi-blast-lowlevel PUBLIC cxx_std_20)
target_compile_definitions(demi-blast-lowlevel PUBLIC
  $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
if(ANDROID)
  target_compile_definitions(demi-blast-lowlevel PUBLIC "NV_C_EXPORT=extern \"C\"")
endif()
set_target_properties(demi-blast-lowlevel PROPERTIES POSITION_INDEPENDENT_CODE ON)
