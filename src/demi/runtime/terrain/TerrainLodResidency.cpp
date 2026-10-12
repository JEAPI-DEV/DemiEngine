#include "demi/runtime/terrain/TerrainLodResidency.h"

#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainSurfaceMaterials.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace demi::runtime {
namespace {

std::size_t addBytes(std::size_t first, std::size_t second) {
  if (second > std::numeric_limits<std::size_t>::max() - first) {
    return std::numeric_limits<std::size_t>::max();
  }
  return first + second;
}

std::size_t multiplyBytes(std::size_t count, std::size_t size) {
  if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
    return std::numeric_limits<std::size_t>::max();
  }
  return count * size;
}

// Count owned array capacities, including indices and material provenance.
// Shared buffers are counted once when old/new binding versions overlap.
std::size_t levelBytes(std::span<const PreparedTerrainLodLevel *const> levels) {
  std::unordered_set<const void *> counted;
  std::size_t bytes = 0;
  const auto count = [&](const PreparedTerrainLodLevel *level) {
    if (!level || !counted.insert(level).second) {
      return;
    }
    bytes = addBytes(bytes, sizeof(*level));
    for (const auto &[biome, surface] : *level) {
      bytes = addBytes(bytes, sizeof(surface) + biome.capacity());
      if (surface.geometry && counted.insert(surface.geometry.get()).second) {
        const auto &mesh = *surface.geometry;
        bytes = addBytes(bytes, sizeof(mesh));
        bytes = addBytes(bytes,
                         multiplyBytes(mesh.vertices.capacity(), sizeof(Vec3)));
        bytes = addBytes(bytes,
                         multiplyBytes(mesh.normals.capacity(), sizeof(Vec3)));
        bytes =
            addBytes(bytes, multiplyBytes(mesh.uvs.capacity(), sizeof(Vec2)));
        bytes = addBytes(
            bytes, multiplyBytes(mesh.vertexColors.capacity(), sizeof(Color)));
      }
      if (surface.sections.indices &&
          counted.insert(surface.sections.indices.get()).second) {
        bytes = addBytes(bytes, sizeof(*surface.sections.indices));
        bytes =
            addBytes(bytes, multiplyBytes(surface.sections.indices->capacity(),
                                          sizeof(std::uint32_t)));
      }
      if (surface.sampleIndices &&
          counted.insert(surface.sampleIndices.get()).second) {
        bytes = addBytes(bytes, sizeof(*surface.sampleIndices));
        bytes = addBytes(bytes, multiplyBytes(surface.sampleIndices->capacity(),
                                              sizeof(std::size_t)));
      }
      bytes =
          addBytes(bytes, multiplyBytes(surface.sections.sections.capacity(),
                                        sizeof(MeshMaterialSection3D)));
      for (const auto &section : surface.sections.sections) {
        bytes = addBytes(bytes,
                         section.id.capacity() + section.material.capacity());
      }
    }
  };
  for (const auto *level : levels) {
    count(level);
  }
  return bytes;
}

std::size_t preparationReservation(const PreparedTerrainChunkLod &descriptor,
                                   std::size_t level) {
  const auto triangles = descriptor.triangleCounts[level];
  // The builder expands vertices and provenance, then partitions indices.
  // Double the array estimate for vector capacity growth and temporary output.
  constexpr std::size_t PerVertex = sizeof(Vec3) * 2 + sizeof(Vec2) +
                                    sizeof(Color) + sizeof(std::size_t) +
                                    sizeof(std::uint32_t);
  auto bytes = multiplyBytes(triangles, 3 * PerVertex * 2);
  if (descriptor.field) {
    for (const auto &biome : descriptor.field->biomeIds) {
      bytes = addBytes(bytes, sizeof(TerrainLodSurface) +
                                  sizeof(MeshRendererComponent) + biome.size() +
                                  128);
    }
    // Material names can be longer than typical small-string storage.
    for (const auto &material : descriptor.field->biomeMaterials) {
      bytes = addBytes(bytes, material.size());
    }
    if (descriptor.field->surfaceMaterials) {
      const auto &bindings = descriptor.field->surfaceMaterials->bindings;
      std::size_t sectionBytes =
          multiplyBytes(bindings.size() + 1, sizeof(MeshMaterialSection3D) * 2);
      for (const auto &[role, material] : bindings) {
        sectionBytes = addBytes(sectionBytes, role.size() + material.size());
      }
      bytes = addBytes(bytes, multiplyBytes(descriptor.field->biomeIds.size(),
                                            sectionBytes));
    }
  }
  return addBytes(bytes, sizeof(PreparedTerrainLodLevel));
}

std::size_t bindingReservation(const PreparedTerrainChunkLod &descriptor,
                               const PreparedTerrainLodLevel &ready) {
  // Rebinding shares vertices and sample provenance. Reserve only copied map
  // payload and potentially repartitioned indices/section strings.
  std::size_t bytes = sizeof(PreparedTerrainLodLevel);
  std::size_t sectionCount = 1;
  std::size_t bindingNames = 0;
  if (descriptor.field && descriptor.field->surfaceMaterials) {
    const auto &bindings = descriptor.field->surfaceMaterials->bindings;
    sectionCount = bindings.size() + 1;
    for (const auto &[role, material] : bindings) {
      bindingNames = addBytes(bindingNames, role.size() + material.size());
    }
  }
  for (const auto &[biome, surface] : ready) {
    bytes = addBytes(bytes, sizeof(TerrainLodSurface) + biome.size() + 128);
    if (surface.geometry) {
      bytes = addBytes(bytes, multiplyBytes(surface.geometry->vertices.size(),
                                            sizeof(std::uint32_t) * 2));
    }
    const auto sections =
        std::max(sectionCount, surface.sections.sections.size());
    bytes = addBytes(
        bytes, multiplyBytes(sections, sizeof(MeshMaterialSection3D) * 2));
    bytes = addBytes(bytes, bindingNames);
    for (const auto &section : surface.sections.sections) {
      bytes = addBytes(bytes, section.id.size() + section.material.size());
    }
  }
  return bytes;
}

} // namespace

struct TerrainLodResidency::Impl {
  struct Key {
    const TerrainLodGeometryIdentity *geometry = nullptr;
    const HeightField *appearance = nullptr;
    std::size_t level = 0;
    bool operator==(const Key &) const = default;
  };
  struct KeyHash {
    std::size_t operator()(const Key &key) const {
      return std::hash<const void *>{}(key.geometry) ^
             (std::hash<const void *>{}(key.appearance) << 1U) ^
             (std::hash<std::size_t>{}(key.level) << 2U);
    }
  };
  struct Outcome {
    std::shared_ptr<const PreparedTerrainLodLevel> level;
    std::string error;
  };
  struct Pending {
    JobHandle job;
    std::stop_source stop;
    std::shared_ptr<Outcome> outcome;
    std::shared_ptr<const PreparedTerrainChunkLod> descriptor;
    std::shared_ptr<const PreparedTerrainLodLevel> rebindSource;
    std::size_t reservedBytes = 0;
  };
  struct Entry {
    std::shared_ptr<const PreparedTerrainChunkLod> descriptor;
    std::shared_ptr<const PreparedTerrainChunkLod> readyDescriptor;
    std::shared_ptr<const PreparedTerrainLodLevel> ready;
    std::unique_ptr<Pending> pending;
    bool terminalFailure = false;
    bool demanded = false;
  };

  explicit Impl(JobSystem &workerPool, Preparation prepare)
      : jobs(workerPool), preparation(std::move(prepare)) {
    if (!preparation) {
      preparation = prepareTerrainRenderLodLevel;
    }
  }

  JobSystem &jobs;
  Preparation preparation;
  Settings settings;
  Statistics counters;
  std::unordered_map<Key, Entry, KeyHash> entries;
  std::size_t startedThisPresentation = 0;
  bool presentationOpen = false;
  std::string error;

  Statistics currentStatistics() const { return counters; }

  void releaseReady(Entry &entry) {
    entry.ready.reset();
    entry.readyDescriptor.reset();
  }

  std::vector<const PreparedTerrainLodLevel *> retainedLevels() const {
    std::vector<const PreparedTerrainLodLevel *> levels;
    for (const auto &[key, entry] : entries) {
      if (entry.ready) {
        levels.push_back(entry.ready.get());
      }
      if (entry.pending && entry.pending->rebindSource) {
        levels.push_back(entry.pending->rebindSource.get());
      }
    }
    return levels;
  }

  void refreshAccounting() {
    counters.readyLevels = 0;
    counters.pendingJobs = 0;
    counters.pendingBytes = 0;
    for (const auto &[key, entry] : entries) {
      counters.readyLevels += entry.ready ? 1 : 0;
      if (entry.pending) {
        ++counters.pendingJobs;
        counters.pendingBytes += entry.pending->reservedBytes;
      }
    }
    const auto levels = retainedLevels();
    counters.residentBytes = levelBytes(levels);
  }

  std::shared_ptr<const PreparedTerrainLodLevel>
  rebindSource(const Key &key) const {
    for (const auto &[candidate, entry] : entries) {
      if (candidate.geometry == key.geometry && candidate.level == key.level &&
          entry.ready) {
        return entry.ready;
      }
    }
    return {};
  }

  bool geometryAlreadyPreparing(const Key &key) const {
    return std::ranges::any_of(entries, [&key](const auto &item) {
      return item.first.geometry == key.geometry &&
             item.first.level == key.level && item.second.pending != nullptr;
    });
  }

  void cancelAbsentDemand() {
    bool released = false;
    for (auto &[key, entry] : entries) {
      if (!entry.demanded) {
        if (entry.pending) {
          // A rebind worker may still borrow the previous ready level. Keep it
          // charged until cancellation completes instead of hiding its bytes.
          entry.pending->stop.request_stop();
        } else if (entry.ready) {
          releaseReady(entry);
          released = true;
        }
      }
    }
    std::erase_if(entries, [](const auto &item) {
      return !item.second.demanded && !item.second.pending;
    });
    if (released) {
      refreshAccounting();
    }
  }

  void schedule(const Key &key, Entry &entry) {
    const auto current = currentStatistics();
    if (jobs.workerCount() == 0 || settings.maximumPendingJobs == 0 ||
        settings.maximumJobsPerPresentation == 0 ||
        settings.maximumResidentBytes == 0) {
      return;
    }
    if (current.pendingJobs >= settings.maximumPendingJobs ||
        startedThisPresentation >= settings.maximumJobsPerPresentation) {
      ++counters.budgetPressure;
      return;
    }
    const auto sourceLevel = rebindSource(key);
    if (!sourceLevel && geometryAlreadyPreparing(key)) {
      return;
    }
    const auto reservation =
        sourceLevel ? bindingReservation(*entry.descriptor, *sourceLevel)
                    : preparationReservation(*entry.descriptor, key.level);
    const auto committed =
        addBytes(current.residentBytes, current.pendingBytes);
    if (committed > settings.maximumResidentBytes ||
        reservation > settings.maximumResidentBytes - committed) {
      ++counters.budgetPressure;
      return;
    }
    auto pending = std::make_unique<Pending>();
    pending->descriptor = entry.descriptor;
    pending->rebindSource = sourceLevel;
    pending->reservedBytes = reservation;
    pending->outcome = std::make_shared<Outcome>();
    const auto source = pending->descriptor;
    const auto outcome = pending->outcome;
    const auto token = pending->stop.get_token();
    const auto oldReady = sourceLevel;
    const auto prepare = preparation;
    try {
      pending->job = jobs.submit([source, outcome, token, oldReady, prepare,
                                  level = key.level] {
        try {
          if (token.stop_requested()) {
            return;
          }
          outcome->level = oldReady
                               ? rebindTerrainRenderLodLevel(*source, *oldReady)
                               : prepare(*source, level, token);
          if (token.stop_requested()) {
            outcome->level.reset();
          }
        } catch (const std::exception &failure) {
          outcome->error = failure.what();
        } catch (...) {
          outcome->error =
              "Terrain LOD preparation failed with an unknown exception";
        }
      });
      entry.pending = std::move(pending);
      ++counters.pendingJobs;
      counters.pendingBytes += reservation;
      ++counters.submittedJobs;
      ++startedThisPresentation;
    } catch (const std::exception &failure) {
      error = failure.what();
      ++counters.failedJobs;
    }
  }
};

TerrainLodResidency::TerrainLodResidency(JobSystem &jobs,
                                         Preparation preparation)
    : impl_(std::make_unique<Impl>(jobs, std::move(preparation))) {}

TerrainLodResidency::~TerrainLodResidency() {
  clear();
  // Jobs capture shared immutable inputs/results, never this service. Joining
  // only at destruction keeps those captures alive until cancellation finishes.
  for (auto &[key, entry] : impl_->entries) {
    if (entry.pending) {
      try {
        entry.pending->job.wait();
      } catch (...) {
        // Destruction must not propagate a worker exception.
      }
    }
  }
}

void TerrainLodResidency::setSettings(Settings settings) {
  if (impl_->settings != settings) {
    for (auto &[key, entry] : impl_->entries) {
      entry.terminalFailure = false;
    }
  }
  impl_->settings = settings;
  const auto current = impl_->currentStatistics();
  if (addBytes(current.residentBytes, current.pendingBytes) >
      settings.maximumResidentBytes) {
    // Dropping the optional variants is always safe: callers fall back to Full.
    // Cancelled worker captures can remain alive until cooperative completion.
    ++impl_->counters.budgetPressure;
    clear();
  }
}

void TerrainLodResidency::beginPresentation() {
  impl_->presentationOpen = false;
  pollCompleted();
  impl_->presentationOpen = true;
  impl_->startedThisPresentation = 0;
  for (auto &[key, entry] : impl_->entries) {
    entry.demanded = false;
  }
}

std::shared_ptr<const PreparedTerrainLodLevel> TerrainLodResidency::request(
    std::shared_ptr<const PreparedTerrainChunkLod> descriptor,
    std::size_t level) {
  if (!descriptor || !descriptor->geometryIdentity || level == 0 ||
      level >= 4) {
    return {};
  }
  const Impl::Key key{descriptor->geometryIdentity.get(),
                      descriptor->field.get(), level};
  auto &entry = impl_->entries[key];
  entry.demanded = true;
  entry.descriptor = std::move(descriptor);
  if (entry.ready) {
    return entry.ready;
  }
  if (!entry.pending && !entry.terminalFailure) {
    impl_->schedule(key, entry);
  }
  return {};
}

void TerrainLodResidency::pollCompleted() {
  // Readiness is frozen while cameras/biome groups consume this presentation.
  if (impl_->presentationOpen) {
    return;
  }
  for (auto &[key, entry] : impl_->entries) {
    if (!entry.pending || !entry.pending->job.ready()) {
      continue;
    }
    // ready() observes the completion mutex; outcome publication precedes it.
    try {
      entry.pending->job.wait();
    } catch (const std::exception &failure) {
      entry.pending->outcome->error = failure.what();
    } catch (...) {
      entry.pending->outcome->error =
          "Terrain LOD worker failed with an unknown exception";
    }
    const auto &outcome = *entry.pending->outcome;
    const bool stale =
        entry.pending->stop.stop_requested() ||
        entry.pending->descriptor->field != entry.descriptor->field ||
        entry.pending->descriptor->geometryIdentity !=
            entry.descriptor->geometryIdentity;
    if (stale) {
      ++impl_->counters.discardedJobs;
    } else if (!outcome.error.empty() || !outcome.level) {
      impl_->error = outcome.error.empty()
                         ? "Terrain LOD preparation produced no level"
                         : outcome.error;
      ++impl_->counters.failedJobs;
      entry.terminalFailure = true;
    } else {
      const auto current = impl_->currentStatistics();
      auto retained = impl_->retainedLevels();
      retained.push_back(outcome.level.get());
      const auto peakBytes = levelBytes(retained);
      const auto otherPending =
          current.pendingBytes - entry.pending->reservedBytes;
      if (otherPending > impl_->settings.maximumResidentBytes ||
          peakBytes > impl_->settings.maximumResidentBytes - otherPending) {
        ++impl_->counters.budgetPressure;
        ++impl_->counters.discardedJobs;
        entry.terminalFailure = true;
        impl_->error = "Terrain LOD prepared payload exceeds the cache budget";
      } else {
        entry.ready = outcome.level;
        entry.readyDescriptor = entry.pending->descriptor;
      }
    }
    entry.pending.reset();
    if (!entry.demanded) {
      impl_->releaseReady(entry);
    }
    impl_->refreshAccounting();
  }
  std::erase_if(impl_->entries, [](const auto &item) {
    return !item.second.demanded && !item.second.pending && !item.second.ready;
  });
}

void TerrainLodResidency::endPresentation() {
  impl_->cancelAbsentDemand();
  impl_->presentationOpen = false;
}

void TerrainLodResidency::tickInactive() {
  impl_->presentationOpen = false;
  for (auto &[key, entry] : impl_->entries) {
    entry.demanded = false;
  }
  impl_->cancelAbsentDemand();
  pollCompleted();
}

void TerrainLodResidency::clear() { tickInactive(); }

TerrainLodResidency::Statistics TerrainLodResidency::statistics() const {
  return impl_->currentStatistics();
}

bool TerrainLodResidency::hasPendingWork() const {
  const auto current = impl_->currentStatistics();
  if (current.pendingJobs != 0) {
    return true;
  }
  if (impl_->jobs.workerCount() == 0 ||
      impl_->settings.maximumPendingJobs == 0 ||
      impl_->settings.maximumJobsPerPresentation == 0) {
    return false;
  }
  const auto committed = addBytes(current.residentBytes, current.pendingBytes);
  if (committed >= impl_->settings.maximumResidentBytes) {
    return false;
  }
  for (const auto &[key, entry] : impl_->entries) {
    if (!entry.demanded || entry.ready || entry.pending ||
        entry.terminalFailure) {
      continue;
    }
    const auto source = impl_->rebindSource(key);
    const auto reservation =
        source ? bindingReservation(*entry.descriptor, *source)
               : preparationReservation(*entry.descriptor, key.level);
    if (reservation <= impl_->settings.maximumResidentBytes - committed) {
      return true;
    }
  }
  return false;
}

const std::string &TerrainLodResidency::lastError() const {
  return impl_->error;
}

} // namespace demi::runtime
