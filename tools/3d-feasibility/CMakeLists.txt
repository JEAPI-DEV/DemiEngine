cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(DemiBlastFeasibility LANGUAGES CXX)
  enable_testing()
endif()

include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
# Repository contains PhysX, but only the audited Blast low-level sources below
# are compiled. Never invoke upstream's full SDK/dependency bootstrap.
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
  # Upstream guards C linkage for Windows/Linux but omits Android. Use its
  # supported override; do not pretend Android is desktop Linux.
  target_compile_definitions(demi-blast-lowlevel PUBLIC "NV_C_EXPORT=extern \"C\"")
endif()
set_target_properties(demi-blast-lowlevel PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(demi-blast-probe blast_probe.cpp BlastFixture.cpp)
target_link_libraries(demi-blast-probe PRIVATE demi-blast-lowlevel)
add_executable(demi-blast-work-benchmark blast_work_benchmark.cpp BlastFixture.cpp)
target_link_libraries(demi-blast-work-benchmark PRIVATE demi-blast-lowlevel)
if(NOT ANDROID)
  add_test(NAME demi-blast-probe COMMAND demi-blast-probe)
  add_test(NAME demi-blast-work-smoke COMMAND demi-blast-work-benchmark --smoke)
endif()

if(TARGET demi-runtime-lib)
  add_executable(demi-blast-jolt-probe jolt_probe.cpp BlastFixture.cpp)
  target_link_libraries(demi-blast-jolt-probe PRIVATE demi-runtime-lib demi-blast-lowlevel)
  if(NOT ANDROID)
    add_test(NAME demi-blast-jolt-probe COMMAND demi-blast-jolt-probe)
  endif()
endif()
