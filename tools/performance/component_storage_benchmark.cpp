#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/DirectionalLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/PointLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SphereCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/ComponentStorage.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace demi::runtime;
struct AllocationStats {
  std::size_t calls = 0;
  std::size_t bytes = 0;
};
AllocationStats allocations;
bool trackAllocations = false;
void *operator new(std::size_t size) {
  if (void *value = std::malloc(size == 0 ? 1 : size)) {
    if (trackAllocations) {
      ++allocations.calls;
      allocations.bytes += size;
    }
    return value;
  }
  throw std::bad_alloc();
}
void operator delete(void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }

// Explicit historical control: the original RTTI-key map recomputes its hash
// for every query. Only this standalone comparison retains that implementation.
class UncachedStorage {
public:
  template <class T> const T *get() const {
    const auto key = std::type_index(typeid(std::remove_cvref_t<T>));
    const auto found = components_.find(key);
    return found == components_.end() ? nullptr
                                      : std::any_cast<T>(&found->second);
  }
  template <class T> void set(T value) {
    components_.insert_or_assign(std::type_index(typeid(T)), std::move(value));
  }

private:
  std::unordered_map<std::type_index, std::any> components_;
};

template <class Storage>
void populate(std::vector<Storage> &entities, std::size_t count) {
  entities.resize(count);
  for (std::size_t index = 0; index < count; ++index) {
    Transform3DComponent transform;
    transform.position.x = float(index % 17);
    entities[index].set(std::move(transform));
    if (index % 100 < 97) {
      entities[index].set(MeshRendererComponent{});
    }
    if (index % 100 < 25) {
      entities[index].set(SphereCollider3DComponent{});
    }
    if (index % 100 == 97) {
      entities[index].set(Camera3DComponent{});
    }
    if (index % 100 == 98) {
      entities[index].set(DirectionalLightComponent{});
    }
    if (index % 100 == 99) {
      entities[index].set(PointLightComponent{});
    }
    if (index == 0) {
      entities[index].set(Terrain3DComponent{});
    }
  }
}

template <class Storage>
[[gnu::noinline]] std::uint64_t scan(const std::vector<Storage> &entities,
                                     int frames, int workload) {
  std::uint64_t checksum = 0;
  for (int frame = 0; frame < frames; ++frame) {
    for (const auto &entity : entities) {
      if (workload != 2) {
        const auto *transform = entity.template get<Transform3DComponent>();
        const auto *mesh = entity.template get<MeshRendererComponent>();
        checksum += transform ? std::uint64_t(transform->position.x) + 1 : 0;
        checksum += mesh ? mesh->revision + 1 : 0;
      }
      if (workload != 0) {
        checksum += entity.template get<Camera3DComponent>() != nullptr;
        checksum += entity.template get<DirectionalLightComponent>() != nullptr;
        checksum += entity.template get<PointLightComponent>() != nullptr;
        checksum += entity.template get<SphereCollider3DComponent>() != nullptr;
        checksum += entity.template get<Terrain3DComponent>() != nullptr;
        checksum += entity.template get<Transform2DComponent>() != nullptr;
      }
    }
  }
  return checksum;
}

template <class Storage>
double measure(const std::vector<Storage> &entities, int frames, int workload,
               std::uint64_t &checksum) {
  const auto start = std::chrono::steady_clock::now();
  checksum = scan(entities, frames, workload);
  const auto end = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(end - start).count() /
         frames;
}
int main(int argc, char **argv) {
  int frames = 120;
  if (argc > 2) {
    std::fprintf(stderr,
                 "usage: component_storage_benchmark [positive frames]\n");
    return 2;
  }
  if (argc == 2) {
    const char *end = argv[1] + std::strlen(argv[1]);
    const auto parsed = std::from_chars(argv[1], end, frames);
    if (parsed.ec != std::errc{} || parsed.ptr != end || frames <= 0) {
      std::fprintf(stderr, "frames must be a positive integer\n");
      return 2;
    }
  }
  constexpr std::size_t Count = 23848;
  std::vector<UncachedStorage> baseline;
  std::vector<ComponentStorage> cached;
  trackAllocations = true;
  populate(baseline, Count);
  trackAllocations = false;
  const auto originalAllocations = allocations;
  allocations = {};
  trackAllocations = true;
  populate(cached, Count);
  trackAllocations = false;
  const auto cachedAllocations = allocations;
  std::printf("entity_count=%zu frames_per_sample=%d storage_bytes=%zu/%zu "
              "\n",
              Count, frames, sizeof(UncachedStorage), sizeof(ComponentStorage));
  std::printf(
      "populate_allocations baseline=%zu/%zu cached=%zu/%zu (calls/bytes)\n",
      originalAllocations.calls, originalAllocations.bytes,
      cachedAllocations.calls, cachedAllocations.bytes);
  std::printf(
      "workload,round,baseline_ms_per_frame,cached_ms_per_frame,checksum\n");
  for (int workload = 0; workload < 3; ++workload) {
    (void)scan(baseline, 3, workload);
    (void)scan(cached, 3, workload);
    for (int round = 0; round < 5; ++round) {
      std::uint64_t originalChecksum = 0, cachedChecksum = 0;
      double originalTime = 0, cachedTime = 0;
      if (round % 2 == 0) {
        originalTime = measure(baseline, frames, workload, originalChecksum);
        cachedTime = measure(cached, frames, workload, cachedChecksum);
      } else {
        cachedTime = measure(cached, frames, workload, cachedChecksum);
        originalTime = measure(baseline, frames, workload, originalChecksum);
      }
      if (originalChecksum != cachedChecksum) {
        throw std::runtime_error("checksum mismatch");
      }
      std::printf("%s,%d,%.6f,%.6f,%llu\n",
                  workload == 0   ? "hits2"
                  : workload == 1 ? "mixed8"
                                  : "mostly_misses6",
                  round, originalTime, cachedTime,
                  static_cast<unsigned long long>(originalChecksum));
      std::fflush(stdout);
    }
  }
}
