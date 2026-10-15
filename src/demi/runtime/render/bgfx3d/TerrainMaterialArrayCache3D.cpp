#include "demi/runtime/render/bgfx3d/TerrainMaterialArrayCache3D.h"

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <stop_token>
#include <utility>

namespace demi::runtime::render {
namespace {

struct AllocationAccounting {
  std::size_t bytes = 0;
};

struct ArrayOwnership {
  GpuResources *resources = nullptr;
  std::shared_ptr<AllocationAccounting> accounting;
  std::array<TextureHandle, terrainMaterialArrayCount3D> textures{};
  std::size_t bytes = 0;

  ~ArrayOwnership() {
    for (const auto handle : textures) {
      if (handle) {
        resources->destroy(handle);
      }
    }
    accounting->bytes -= bytes;
  }
};

using Key = std::pair<std::string, std::uint16_t>;

} // namespace

struct TerrainMaterialArrayCache3D::Impl {
  struct Outcome {
    std::optional<TerrainMaterialArrayData3D> data;
    std::string error;
  };
  struct Entry {
    std::shared_ptr<const TerrainMaterialArraySource3D> source;
    std::shared_ptr<const TerrainMaterialArrays3D> ready;
    std::shared_ptr<Outcome> outcome;
    JobHandle job;
    std::stop_source cancellation;
    std::size_t reservation = 0;
    std::string failure;
    bool touched = false;
    bool requested = false;
    bool retryable = false;
    bool waitingForGpu = false;
    std::size_t blockedBytes = 0;
  };

  GpuResources &resources;
  JobSystem &jobs;
  std::shared_ptr<AllocationAccounting> accounting =
      std::make_shared<AllocationAccounting>();
  std::map<Key, Entry> entries;
  std::size_t gpuBudget = 64U * 1024U * 1024U;
  std::size_t stagingBudget = 128U * 1024U * 1024U;
  std::size_t jobBudget = 2;
  std::size_t uploaded = 0;

  Impl(GpuResources &gpu, JobSystem &pool) : resources(gpu), jobs(pool) {}

  std::size_t pendingBytes() const {
    std::size_t total = 0;
    for (const auto &[key, entry] : entries) {
      total += entry.reservation;
    }
    return total;
  }

  std::size_t pendingJobs() const {
    return std::count_if(entries.begin(), entries.end(), [](const auto &item) {
      return bool(item.second.job);
    });
  }

  void retireUnusedReady() {
    for (auto &[key, entry] : entries) {
      if (!entry.touched) {
        entry.ready.reset();
      }
    }
  }

  std::shared_ptr<const TerrainMaterialArrays3D>
  upload(Entry &entry, const TerrainMaterialArrayData3D &data,
         std::string &error) {
    if (data.bytes() > gpuBudget ||
        accounting->bytes > gpuBudget - data.bytes()) {
      error = "Terrain material arrays exceed the resident texture budget.";
      return {};
    }
    auto ownership = std::make_shared<ArrayOwnership>();
    ownership->resources = &resources;
    ownership->accounting = accounting;
    for (std::size_t index = 0; index < data.pixels.size(); ++index) {
      const bool color = index == 0 || index == 4;
      const TextureCreateInfo info{.width = data.arrayEdges[index],
                                   .height = data.arrayEdges[index],
                                   .format = TextureFormat::RGBA8,
                                   .data = data.pixels[index],
                                   .generateMipmaps = true,
                                   .srgb = color,
                                   .filter = TextureFilter::Linear,
                                   .wrap = TextureWrap::Repeat,
                                   .debugName = "terrain-material-array-" +
                                                std::to_string(index),
                                   .layers = data.physicalLayers};
      ownership->textures[index] = resources.createTexture(info, error);
      if (!ownership->textures[index]) {
        return {};
      }
    }
    ownership->bytes = data.bytes();
    accounting->bytes += ownership->bytes;
    uploaded += ownership->bytes;
    auto ready = std::make_shared<TerrainMaterialArrays3D>();
    ready->source = entry.source;
    ready->textures = ownership->textures;
    ready->edge = data.edge;
    ready->residentBytes = data.bytes();
    ready->ownership_ = std::move(ownership);
    return ready;
  }

  void releaseEncodedInputs(Entry &entry) {
    auto metadata =
        std::make_shared<TerrainMaterialArraySource3D>(*entry.source);
    for (auto &layer : metadata->layers) {
      for (auto &[slot, image] : layer.images) {
        if (image.encoded) {
          image.encodedBytes = image.encoded->size();
          image.encoded.reset();
        }
      }
    }
    entry.source = std::move(metadata);
  }

  void pollCompleted() {
    for (auto &[key, entry] : entries) {
      if (entry.job) {
        if (!entry.job.ready()) {
          continue;
        }
        try {
          entry.job.wait();
        } catch (const std::exception &exception) {
          entry.failure = exception.what();
        }
        entry.job = {};
        releaseEncodedInputs(entry);
      }
      if (!entry.outcome) {
        continue;
      }
      if (entry.cancellation.stop_requested()) {
        entry.outcome.reset();
        entry.reservation = 0;
        entry.cancellation = {};
        entry.waitingForGpu = false;
        continue;
      }
      if (!entry.failure.empty() || !entry.outcome->data) {
        if (entry.failure.empty()) {
          entry.failure = entry.outcome->error.empty()
                              ? "Terrain material preparation did not produce "
                                "a complete array set."
                              : entry.outcome->error;
        }
        entry.outcome.reset();
        entry.reservation = 0;
        continue;
      }
      const auto &data = *entry.outcome->data;
      if (data.bytes() > gpuBudget) {
        entry.failure =
            "Terrain material arrays exceed the resident texture budget.";
        entry.outcome.reset();
        entry.reservation = 0;
        continue;
      }
      if (accounting->bytes > gpuBudget - data.bytes()) {
        // A prepared replacement can retire an older fallback tier, but never
        // a tier explicitly requested by another camera in the same union.
        for (auto &[otherKey, candidate] : entries) {
          if (otherKey.first == key.first && otherKey != key &&
              !candidate.requested) {
            candidate.ready.reset();
          }
        }
      }
      if (accounting->bytes > gpuBudget - data.bytes()) {
        entry.waitingForGpu = true;
        entry.blockedBytes = accounting->bytes;
        continue;
      }
      entry.waitingForGpu = false;
      try {
        const auto replacement = upload(entry, data, entry.failure);
        if (replacement) {
          entry.ready = replacement;
        }
      } catch (const std::exception &exception) {
        entry.failure = exception.what();
      }
      entry.outcome.reset();
      entry.reservation = 0;
      entry.cancellation = {};
    }
  }

  void clear() {
    for (auto &[key, entry] : entries) {
      entry.cancellation.request_stop();
    }
    for (auto &[key, entry] : entries) {
      if (entry.job) {
        try {
          entry.job.wait();
        } catch (...) {
          // Shutdown must join every immutable worker input before destruction.
        }
      }
    }
    entries.clear();
  }
};

TerrainMaterialArrayCache3D::TerrainMaterialArrayCache3D(
    GpuResources &resources, JobSystem &jobs)
    : impl_(std::make_unique<Impl>(resources, jobs)) {}

TerrainMaterialArrayCache3D::~TerrainMaterialArrayCache3D() { impl_->clear(); }

void TerrainMaterialArrayCache3D::setBudgets(std::size_t gpuBytes,
                                             std::size_t stagingBytes,
                                             std::size_t preparationJobs) {
  if (impl_->gpuBudget == gpuBytes && impl_->stagingBudget == stagingBytes &&
      impl_->jobBudget == preparationJobs) {
    return;
  }
  impl_->gpuBudget = gpuBytes;
  impl_->stagingBudget = stagingBytes;
  impl_->jobBudget = preparationJobs;
  for (auto &[key, entry] : impl_->entries) {
    entry.failure.clear();
    entry.waitingForGpu = false;
    if (impl_->accounting->bytes > gpuBytes) {
      entry.ready.reset();
    }
    if (entry.job &&
        (preparationJobs == 0 || entry.reservation > stagingBytes)) {
      entry.cancellation.request_stop();
    }
  }
}

void TerrainMaterialArrayCache3D::beginPresentation() {
  impl_->uploaded = 0;
  // Completion publication happens once, before the first camera or shadow.
  impl_->pollCompleted();
  for (auto &[key, entry] : impl_->entries) {
    entry.touched = false;
    entry.requested = false;
    entry.retryable = false;
  }
}

void TerrainMaterialArrayCache3D::endPresentation() {
  for (auto &[key, entry] : impl_->entries) {
    if (!entry.touched && (entry.job || entry.outcome)) {
      entry.cancellation.request_stop();
    }
  }
  impl_->retireUnusedReady();
  for (auto iterator = impl_->entries.begin();
       iterator != impl_->entries.end();) {
    const auto &entry = iterator->second;
    if (!entry.touched && !entry.job && !entry.ready && !entry.outcome) {
      iterator = impl_->entries.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

void TerrainMaterialArrayCache3D::clear() { impl_->clear(); }
void TerrainMaterialArrayCache3D::invalidate() {
  for (auto &[key, entry] : impl_->entries) {
    entry.cancellation.request_stop();
    entry.ready.reset();
    entry.failure.clear();
    entry.touched = false;
  }
}

TerrainMaterialArrayRequest3D TerrainMaterialArrayCache3D::request(
    std::shared_ptr<const TerrainMaterialArraySource3D> source,
    std::uint16_t maximumEdge) {
  if (!source || source->identity.empty() || source->layers.empty() ||
      maximumEdge == 0) {
    return {.fallbackReason =
                "Terrain material source or quality resolution is invalid."};
  }
  if (impl_->gpuBudget == 0) {
    return {.fallbackReason = "Terrain material array residency is disabled by "
                              "its zero texture budget."};
  }
  const auto limits = impl_->resources.limits();
  const auto capabilities =
      impl_->resources.textureCapabilities(TextureFormat::RGBA8);
  const auto physicalLayers = std::max(std::size_t{2}, source->layers.size());
  if (!capabilities.sampledArray || physicalLayers > limits.maxTextureLayers ||
      physicalLayers > UINT16_MAX) {
    return {.fallbackReason = "Terrain material array layers exceed this "
                              "graphics device's support."};
  }
  maximumEdge = static_cast<std::uint16_t>(
      std::min<std::uint32_t>(maximumEdge, limits.maxTextureSize));
  auto &entry = impl_->entries[{source->identity, maximumEdge}];
  entry.touched = true;
  entry.requested = true;
  entry.retryable = false;
  if (!entry.source ||
      (!entry.ready && !entry.job && !entry.outcome && entry.failure.empty())) {
    entry.source = std::move(source);
  }
  auto ready = entry.ready;
  // Keep an already resident tier usable while its replacement is prepared.
  // Camera demand can pin different tiers of the same source in one frame.
  if (!ready) {
    std::uint32_t closest = UINT32_MAX;
    for (auto &[key, candidate] : impl_->entries) {
      if (key.first != entry.source->identity || !candidate.ready) {
        continue;
      }
      const auto distance =
          unsigned(std::abs(int(key.second) - int(maximumEdge)));
      if (distance < closest) {
        ready = candidate.ready;
        closest = distance;
      }
    }
    if (ready) {
      for (auto &[key, candidate] : impl_->entries) {
        if (candidate.ready == ready) {
          candidate.touched = true;
        }
      }
    }
  }
  if (entry.ready || !entry.failure.empty() || entry.job || entry.outcome) {
    return {.arrays = ready,
            .preparing = bool(entry.job) ||
                         (bool(entry.outcome) && !entry.waitingForGpu),
            .fallbackReason = entry.waitingForGpu
                                  ? "Terrain material replacement is waiting "
                                    "for resident texture memory."
                                  : entry.failure};
  }
  if (terrainMaterialArrayBytes3D(*entry.source, maximumEdge) >
      impl_->gpuBudget) {
    entry.failure =
        "Terrain material arrays exceed the resident texture budget.";
    impl_->releaseEncodedInputs(entry);
    return {.arrays = ready, .fallbackReason = entry.failure};
  }
  if (impl_->jobs.workerCount() == 0 || impl_->jobBudget == 0) {
    impl_->releaseEncodedInputs(entry);
    return {.arrays = ready,
            .fallbackReason =
                "Terrain material asynchronous preparation is disabled."};
  }
  if (impl_->pendingJobs() >= impl_->jobBudget) {
    entry.retryable = true;
    impl_->releaseEncodedInputs(entry);
    return {.arrays = ready,
            .preparing = true,
            .fallbackReason = "Terrain material preparation is waiting for a "
                              "worker budget slot."};
  }
  for (const auto &layer : entry.source->layers) {
    for (const auto &[slot, textureId] : layer.surface.maps) {
      const auto image = layer.images.find(slot);
      if (image == layer.images.end() || !image->second.encoded) {
        return {.arrays = ready, .needsSourceSnapshot = true};
      }
    }
  }
  const auto reservation =
      terrainMaterialArrayReservation3D(*entry.source, maximumEdge);
  const auto pending = impl_->pendingBytes();
  if (reservation > impl_->stagingBudget ||
      pending > impl_->stagingBudget - reservation) {
    entry.retryable = reservation <= impl_->stagingBudget;
    // Permanent oversize is cached; transient pressure retries after other
    // jobs.
    impl_->releaseEncodedInputs(entry);
    if (reservation > impl_->stagingBudget) {
      entry.failure =
          "Terrain material preparation exceeds its staging memory budget.";
    }
    return {
        .arrays = ready,
        .fallbackReason =
            entry.failure.empty()
                ? "Terrain material preparation is waiting for staging memory."
                : entry.failure};
  }
  entry.outcome = std::make_shared<Impl::Outcome>();
  const auto outcome = entry.outcome;
  const auto immutableSource = entry.source;
  const auto stop = entry.cancellation.get_token();
  entry.reservation = reservation;
  try {
    entry.job =
        impl_->jobs.submit([immutableSource, outcome, maximumEdge, stop] {
          outcome->data = prepareTerrainMaterialArrayData3D(
              *immutableSource, maximumEdge, outcome->error, stop);
        });
  } catch (const std::exception &exception) {
    entry.outcome.reset();
    entry.reservation = 0;
    entry.failure = exception.what();
  }
  return {.arrays = ready,
          .preparing = bool(entry.job),
          .fallbackReason = entry.failure};
}

bool TerrainMaterialArrayCache3D::hasPendingWork() const {
  for (const auto &[key, entry] : impl_->entries) {
    if (entry.job) {
      return true;
    }
    if (entry.outcome && (!entry.waitingForGpu ||
                          entry.blockedBytes != impl_->accounting->bytes)) {
      return true;
    }
    const bool workerAvailable = impl_->jobs.workerCount() != 0 &&
                                 impl_->pendingJobs() < impl_->jobBudget;
    if (entry.touched && entry.retryable && workerAvailable) {
      const auto reservation =
          terrainMaterialArrayReservation3D(*entry.source, key.second);
      const auto pending = impl_->pendingBytes();
      if (reservation <= impl_->stagingBudget &&
          pending <= impl_->stagingBudget - reservation) {
        return true;
      }
    }
  }
  return false;
}
std::size_t TerrainMaterialArrayCache3D::residentBytes() const {
  return impl_->accounting->bytes;
}
std::size_t TerrainMaterialArrayCache3D::pendingBytes() const {
  return impl_->pendingBytes();
}
std::size_t TerrainMaterialArrayCache3D::pendingJobs() const {
  return impl_->pendingJobs();
}
std::size_t TerrainMaterialArrayCache3D::uploadBytes() const {
  return impl_->uploaded;
}

} // namespace demi::runtime::render
