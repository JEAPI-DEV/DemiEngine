#include "demi/runtime/render/bgfx3d/TerrainGpuFieldCache3D.h"
#include "demi/runtime/terrain/TerrainSurfaceMaterials.h"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stop_token>
#include <unordered_map>

namespace demi::runtime::render {

struct TerrainGpuAllocationAccounting3D {
  std::size_t bytes = 0;
};

struct TerrainGpuTexture3D {
  GpuResources *resources = nullptr;
  TextureHandle handle;
  std::shared_ptr<TerrainGpuAllocationAccounting3D> accounting;
  std::size_t bytes = 0;

  ~TerrainGpuTexture3D() {
    if (resources && handle) {
      resources->destroy(handle);
    }
    if (accounting) {
      accounting->bytes -= bytes;
    }
  }
};

namespace {

class BudgetFailure final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

using MaterialSnapshot = std::map<std::string, MaterialBinding, std::less<>>;

std::shared_ptr<const MaterialSnapshot>
snapshotMaterials(const HeightField &field,
                  const TerrainGpuMaterialResolver3D &resolve) {
  std::set<std::string, std::less<>> ids;
  for (const auto &id : field.biomeMaterials) {
    if (!id.empty()) {
      ids.insert(id);
    }
  }
  if (field.surfaceMaterials) {
    for (const auto &[role, id] : field.surfaceMaterials->bindings) {
      if (!id.empty()) {
        ids.insert(id);
      }
    }
  }
  auto result = std::make_shared<MaterialSnapshot>();
  for (const auto &id : ids) {
    const auto *source = resolve ? resolve(id) : nullptr;
    if (!source) {
      continue;
    }
    auto [entry, inserted] = result->emplace(id, *source);
    auto &copy = entry->second;
    copy.uniforms.clear();
    for (const auto &uniform : source->uniforms) {
      const auto parameter = std::find_if(
          copy.parameters.begin(), copy.parameters.end(),
          [&](const auto &value) { return value.uniform == uniform.handle; });
      // The packing helper rejects unclassified overrides. Known spans must
      // borrow the immutable copy, never a live MaterialLibrary allocation.
      copy.uniforms.push_back(
          {.handle = uniform.handle,
           .values = parameter == copy.parameters.end()
                         ? std::span<const float>{}
                         : std::span<const float>{parameter->value},
           .count = uniform.count});
    }
  }
  return result;
}

template <typename T>
bool sharedPages(const TerrainSamples<T> &first,
                 const TerrainSamples<T> &second) {
  return first.sharesStorageWith(second);
}

bool sameDimensions(const HeightField &first, const HeightField &second) {
  return first.cellsX == second.cellsX && first.cellsZ == second.cellsZ;
}

std::size_t addBytes(std::size_t first, std::size_t second) {
  const auto maximum = std::numeric_limits<std::size_t>::max();
  return second > maximum - first ? maximum : first + second;
}

std::size_t multiplyBytes(std::size_t first, std::size_t second) {
  const auto maximum = std::numeric_limits<std::size_t>::max();
  return first != 0 && second > maximum / first ? maximum : first * second;
}

std::size_t reserveFor(const HeightField &field,
                       std::uint32_t maximumTextureSize) {
  if (field.cellsX <= 0 || field.cellsZ <= 0) {
    return 0;
  }
  const auto samples = multiplyBytes(std::size_t(field.cellsX) + 1,
                                     std::size_t(field.cellsZ) + 1);
  const auto roles =
      field.surfaceMaterials && field.surfaceMaterials->classification
          ? field.surfaceMaterials->classification->roles.size()
          : 1;
  const auto combinations =
      std::min(samples, multiplyBytes(field.biomeIds.size(),
                                      std::max(std::size_t{1}, roles)));
  // Packed arrays plus palette/map working storage and one padded palette row.
  return addBytes(multiplyBytes(samples, field.morphFromHeights ? 36 : 24),
                  addBytes(multiplyBytes(combinations, 256),
                           multiplyBytes(maximumTextureSize, 16)));
}

} // namespace

struct TerrainGpuFieldCache3D::Impl {
  struct Outcome {
    std::optional<TerrainGpuFieldData3D> data;
    std::string error;
  };
  struct Pending {
    std::shared_ptr<const HeightField> source;
    std::shared_ptr<const TerrainGpuField3D> reuse;
    std::shared_ptr<Outcome> outcome;
    JobHandle job;
    std::stop_source cancellation;
    std::size_t reserve = 0;
    std::uint64_t epoch = 0;
  };
  struct Entry {
    std::shared_ptr<const HeightField> source;
    std::shared_ptr<const TerrainGpuField3D> ready;
    Pending *pending = nullptr;
    std::uint64_t readyEpoch = 0;
    bool demanded = false;
    bool retry = false;
    bool budgetFailure = false;
    std::string reason;
  };

  GpuResources &resources;
  JobSystem &jobs;
  std::unordered_map<const HeightField *, Entry> entries;
  std::vector<std::unique_ptr<Pending>> pending;
  // Charge allocations once, including textures borrowed after cache eviction.
  std::shared_ptr<TerrainGpuAllocationAccounting3D> accounting =
      std::make_shared<TerrainGpuAllocationAccounting3D>();
  std::size_t gpuBudget = 128U * 1024U * 1024U;
  std::size_t stagingBudget = 64U * 1024U * 1024U;
  std::size_t jobBudget = 2;
  std::size_t dispatched = 0;
  std::size_t uploaded = 0;
  std::uint64_t epoch = 0;
  bool presentationOpen = false;

  Impl(GpuResources &gpu, JobSystem &pool) : resources(gpu), jobs(pool) {}

  std::size_t residentBytes() const { return accounting->bytes; }
  std::size_t pendingBytes() const {
    std::size_t bytes = 0;
    for (const auto &job : pending) {
      bytes = addBytes(bytes, job->reserve);
    }
    return bytes;
  }
  std::string capabilityReason() const {
    const auto limits = resources.limits();
    if (!limits.supportsInstancing || limits.maxTextureSamplers < 7) {
      return "GPU terrain requires instancing and seven texture samplers";
    }
    for (const auto format : {TextureFormat::R32F, TextureFormat::RGBA32F}) {
      const auto support = resources.textureCapabilities(format);
      if (!support.sampled2D || !support.vertexSampling) {
        return "GPU terrain height/normal vertex texture sampling is "
               "unsupported";
      }
    }
    if (!resources.textureCapabilities(TextureFormat::RGBA8).sampled2D) {
      return "GPU terrain classification texture sampling is unsupported";
    }
    return {};
  }
  std::shared_ptr<const TerrainGpuField3D>
  reuseFor(const HeightField &field) const {
    for (const auto &[key, entry] : entries) {
      if (entry.ready && sameDimensions(*entry.ready->source, field) &&
          (sharedPages(entry.ready->source->heights, field.heights) ||
           sharedPages(entry.ready->source->normals, field.normals))) {
        return entry.ready;
      }
    }
    return {};
  }
  std::shared_ptr<TerrainGpuTexture3D>
  upload(std::uint16_t width, std::uint16_t height, TextureFormat format,
         std::span<const std::byte> bytes, std::string_view name) {
    const auto used = residentBytes();
    if (bytes.size() > gpuBudget || used > gpuBudget - bytes.size()) {
      throw BudgetFailure("GPU terrain texture budget is exhausted");
    }
    auto owner = std::make_shared<TerrainGpuTexture3D>();
    owner->resources = &resources;
    owner->bytes = bytes.size();
    std::string error;
    owner->handle = resources.createTexture({.width = width,
                                             .height = height,
                                             .format = format,
                                             .data = bytes,
                                             .filter = TextureFilter::Nearest,
                                             .wrap = TextureWrap::Clamp,
                                             .debugName = std::string(name)},
                                            error);
    if (!owner->handle) {
      throw std::runtime_error(
          error.empty() ? "GPU terrain texture allocation failed" : error);
    }
    accounting->bytes += bytes.size();
    owner->accounting = accounting;
    uploaded = addBytes(uploaded, bytes.size());
    return owner;
  }
  std::shared_ptr<const TerrainGpuField3D>
  install(Pending &job, TerrainGpuFieldData3D &data) {
    auto result = std::make_shared<TerrainGpuField3D>();
    result->source = job.source;
    const bool reuseHeight =
        job.reuse && sameDimensions(*job.reuse->source, *job.source) &&
        sharedPages(job.reuse->source->heights, job.source->heights) &&
        job.reuse->source->morphFromHeights == job.source->morphFromHeights;
    const bool reuseNormal =
        job.reuse && sameDimensions(*job.reuse->source, *job.source) &&
        sharedPages(job.reuse->source->normals, job.source->normals);
    const bool reuseIds =
        job.reuse && sameDimensions(*job.reuse->source, *job.source) &&
        job.reuse->ids_ &&
        job.reuse->ids_->size() == data.materialIndices.size() &&
        *job.reuse->ids_ == data.materialIndices;
    std::size_t additional = data.palette.size() * sizeof(data.palette.front());
    if (!reuseHeight) {
      additional = addBytes(additional, data.heights.size() * sizeof(float));
    }
    if (!reuseNormal) {
      additional = addBytes(additional,
                            data.normals.size() * sizeof(data.normals.front()));
    }
    if (!reuseIds) {
      additional =
          addBytes(additional, data.materialIndices.size() *
                                   sizeof(data.materialIndices.front()));
    }
    if (additional > gpuBudget || residentBytes() > gpuBudget - additional) {
      throw BudgetFailure(
          "GPU terrain replacement cannot fit its complete texture bundle");
    }
    result->textures_[0] =
        reuseHeight
            ? job.reuse->textures_[0]
            : upload(data.sampleWidth, data.sampleHeight, data.heightFormat,
                     std::as_bytes(std::span(data.heights)),
                     "Terrain GPU heights");
    result->textures_[1] = reuseNormal
                               ? job.reuse->textures_[1]
                               : upload(data.sampleWidth, data.sampleHeight,
                                        TextureFormat::RGBA32F,
                                        std::as_bytes(std::span(data.normals)),
                                        "Terrain GPU normals");
    result->textures_[2] =
        reuseIds
            ? job.reuse->textures_[2]
            : upload(data.sampleWidth, data.sampleHeight, TextureFormat::RGBA8,
                     std::as_bytes(std::span(data.materialIndices)),
                     "Terrain GPU material indices");
    result->textures_[3] =
        upload(data.paletteWidth, data.paletteHeight, TextureFormat::RGBA32F,
               std::as_bytes(std::span(data.palette)), "Terrain GPU palette");
    result->ids_ =
        reuseIds
            ? job.reuse->ids_
            : std::make_shared<const std::vector<std::array<std::uint8_t, 4>>>(
                  std::move(data.materialIndices));
    result->height = result->textures_[0]->handle;
    result->normal = result->textures_[1]->handle;
    result->classification = result->textures_[2]->handle;
    result->palette = result->textures_[3]->handle;
    result->fieldParameters = {float(data.sampleWidth),
                               float(data.sampleHeight),
                               job.source->size.x / job.source->cellsX,
                               job.source->size.y / job.source->cellsZ};
    result->paletteParameters = {float(data.paletteWidth),
                                 float(data.paletteHeight),
                                 float(data.paletteEntries & 0xffffU),
                                 float(data.paletteEntries >> 16U)};
    result->state = data.state;
    result->mapped = data.mapped;
    result->requiresFragmentCulling = data.requiresFragmentCulling;
    result->materialIds = std::move(data.materialIds);
    return result;
  }
};

TerrainGpuFieldCache3D::TerrainGpuFieldCache3D(GpuResources &resources,
                                               JobSystem &jobs)
    : impl_(std::make_unique<Impl>(resources, jobs)) {}
TerrainGpuFieldCache3D::~TerrainGpuFieldCache3D() { clear(); }

void TerrainGpuFieldCache3D::setBudgets(std::size_t gpuBytes,
                                        std::size_t stagingBytes,
                                        std::size_t preparationJobs) {
  const bool increased = gpuBytes > impl_->gpuBudget ||
                         stagingBytes > impl_->stagingBudget ||
                         preparationJobs > impl_->jobBudget;
  impl_->gpuBudget = gpuBytes;
  impl_->stagingBudget = stagingBytes;
  impl_->jobBudget = preparationJobs;
  if (gpuBytes == 0 || impl_->residentBytes() > gpuBytes) {
    clear();
    return;
  }
  if (impl_->pendingBytes() > stagingBytes) {
    for (const auto &job : impl_->pending) {
      job->cancellation.request_stop();
    }
    for (auto &[key, entry] : impl_->entries) {
      entry.pending = nullptr;
    }
  }
  if (stagingBytes == 0 || preparationJobs == 0) {
    for (auto &[key, entry] : impl_->entries) {
      entry.retry = false;
    }
    return;
  }
  if (increased) {
    for (auto &[key, entry] : impl_->entries) {
      if (entry.budgetFailure) {
        entry.reason.clear();
        entry.retry = true;
        entry.budgetFailure = false;
      }
    }
  }
}
void TerrainGpuFieldCache3D::beginPresentation() {
  impl_->presentationOpen = false;
  impl_->uploaded = 0;
  pollCompleted();
  impl_->dispatched = 0;
  for (auto &[key, entry] : impl_->entries) {
    entry.demanded = false;
  }
  impl_->presentationOpen = true;
}
void TerrainGpuFieldCache3D::endPresentation() {
  impl_->presentationOpen = false;
  std::erase_if(impl_->entries, [](auto &item) {
    auto &entry = item.second;
    if (entry.demanded) {
      return false;
    }
    if (entry.pending) {
      entry.pending->cancellation.request_stop();
    }
    return true;
  });
}
void TerrainGpuFieldCache3D::clear() {
  ++impl_->epoch;
  for (const auto &job : impl_->pending) {
    job->cancellation.request_stop();
  }
  impl_->entries.clear();
}
void TerrainGpuFieldCache3D::invalidateMaterials() {
  ++impl_->epoch;
  for (const auto &job : impl_->pending) {
    job->cancellation.request_stop();
  }
  // Retain the old immutable textures for exact channel reuse and transactional
  // rollback. They are not returned for new draws until current materials pass.
  for (auto &[key, entry] : impl_->entries) {
    entry.pending = nullptr;
    entry.retry = true;
    entry.budgetFailure = false;
    entry.reason.clear();
  }
}

void TerrainGpuFieldCache3D::pollCompleted() {
  if (impl_->presentationOpen) {
    return;
  }
  for (auto iterator = impl_->pending.begin();
       iterator != impl_->pending.end();) {
    auto &job = **iterator;
    if (!job.job.ready()) {
      ++iterator;
      continue;
    }
    try {
      job.job.wait();
    } catch (const std::exception &failure) {
      job.outcome->error = failure.what();
    } catch (...) {
      job.outcome->error = "GPU terrain preparation raised an unknown failure";
    }
    const auto found = impl_->entries.find(job.source.get());
    const bool current =
        found != impl_->entries.end() && found->second.pending == &job &&
        job.epoch == impl_->epoch && !job.cancellation.stop_requested();
    if (current) {
      auto &entry = found->second;
      entry.pending = nullptr;
      try {
        if (!job.outcome->error.empty()) {
          throw std::runtime_error(job.outcome->error);
        }
        if (!job.outcome->data || !job.outcome->data->compatible) {
          entry.reason = job.outcome->data
                             ? job.outcome->data->fallbackReason
                             : "GPU terrain preparation was cancelled";
        } else {
          entry.ready = impl_->install(job, *job.outcome->data);
          entry.readyEpoch = impl_->epoch;
          entry.reason.clear();
        }
      } catch (const BudgetFailure &failure) {
        entry.reason = failure.what();
        entry.budgetFailure = true;
      } catch (const std::exception &failure) {
        entry.reason = failure.what();
      }
    }
    iterator = impl_->pending.erase(iterator);
  }
}
void TerrainGpuFieldCache3D::touch(
    const std::shared_ptr<const HeightField> &field) {
  if (const auto found = impl_->entries.find(field.get());
      found != impl_->entries.end()) {
    found->second.demanded = true;
  }
}
TerrainGpuFieldRequest3D
TerrainGpuFieldCache3D::request(std::shared_ptr<const HeightField> field,
                                const MaterialLibrary &materials) {
  return request(std::move(field),
                 [&](std::string_view id) { return materials.find(id); });
}
TerrainGpuFieldRequest3D
TerrainGpuFieldCache3D::request(std::shared_ptr<const HeightField> field,
                                const TerrainGpuMaterialResolver3D &resolver) {
  if (!field) {
    return {.fallbackReason =
                "GPU terrain requires an immutable field snapshot"};
  }
  if (impl_->gpuBudget == 0) {
    return {.fallbackReason = "GPU terrain is disabled by its GPU budget"};
  }
  if (const auto reason = impl_->capabilityReason(); !reason.empty()) {
    return {.fallbackReason = reason};
  }
  auto [found, inserted] = impl_->entries.try_emplace(field.get());
  auto &entry = found->second;
  entry.source = field;
  entry.demanded = true;
  if (entry.ready && entry.readyEpoch == impl_->epoch) {
    return {.field = entry.ready};
  }
  if (entry.pending) {
    return {.preparing = true};
  }
  if (impl_->stagingBudget == 0 || impl_->jobBudget == 0) {
    return {.fallbackReason =
                "New GPU terrain preparation is disabled by its budget"};
  }
  if (impl_->jobs.workerCount() == 0) {
    return {.fallbackReason =
                "GPU terrain preparation requires a background worker"};
  }
  if (!entry.reason.empty() && !entry.retry) {
    return {.fallbackReason = entry.reason};
  }
  const auto limit = impl_->resources.limits().maxTextureSize;
  if (field->cellsX <= 0 || field->cellsZ <= 0 ||
      std::uint64_t(field->cellsX) + 1 > limit ||
      std::uint64_t(field->cellsZ) + 1 > limit) {
    entry.reason = "GPU terrain field exceeds device texture dimensions";
    entry.retry = false;
    return {.fallbackReason = entry.reason};
  }
  const auto reserve = reserveFor(*field, limit);
  if (reserve > impl_->stagingBudget) {
    entry.reason = "GPU terrain field exceeds CPU staging budget";
    entry.retry = false;
    entry.budgetFailure = true;
    return {.fallbackReason = entry.reason};
  }
  if (impl_->pending.size() >= impl_->jobBudget ||
      impl_->dispatched >= impl_->jobBudget ||
      impl_->pendingBytes() > impl_->stagingBudget - reserve) {
    entry.retry = true;
    return {.preparing = true};
  }
  const auto previousPendingCount = impl_->pending.size();
  try {
    auto job = std::make_unique<Impl::Pending>();
    job->source = field;
    job->reuse = impl_->reuseFor(*field);
    job->reserve = reserve;
    job->epoch = impl_->epoch;
    job->outcome = std::make_shared<Impl::Outcome>();
    const auto snapshot = snapshotMaterials(*field, resolver);
    const auto outcome = job->outcome;
    const auto stop = job->cancellation.get_token();
    impl_->pending.push_back(std::move(job));
    auto &queued = *impl_->pending.back();
    queued.job = impl_->jobs.submit([field, snapshot, outcome, limit, stop] {
      if (stop.stop_requested()) {
        return;
      }
      const auto resolve = [&](std::string_view id) -> const MaterialBinding * {
        const auto found = snapshot->find(id);
        return found == snapshot->end() ? nullptr : &found->second;
      };
      auto data = prepareTerrainGpuFieldData3D(*field, resolve, limit);
      if (!stop.stop_requested()) {
        outcome->data = std::move(data);
      }
    });
    entry.pending = &queued;
    entry.retry = false;
    entry.budgetFailure = false;
    entry.reason.clear();
    ++impl_->dispatched;
  } catch (const std::exception &failure) {
    if (impl_->pending.size() != previousPendingCount) {
      impl_->pending.pop_back();
    }
    entry.pending = nullptr;
    entry.retry = false;
    entry.reason = std::string("GPU terrain request failed: ") + failure.what();
    return {.fallbackReason = entry.reason};
  }
  return {.preparing = true};
}
bool TerrainGpuFieldCache3D::hasPendingWork() const {
  return !impl_->pending.empty() ||
         std::any_of(impl_->entries.begin(), impl_->entries.end(),
                     [](const auto &item) { return item.second.retry; });
}
std::size_t TerrainGpuFieldCache3D::residentBytes() const {
  return impl_->residentBytes();
}
std::size_t TerrainGpuFieldCache3D::pendingBytes() const {
  return impl_->pendingBytes();
}
std::size_t TerrainGpuFieldCache3D::pendingJobs() const {
  return impl_->pending.size();
}
std::size_t TerrainGpuFieldCache3D::uploadBytes() const {
  return impl_->uploaded;
}

} // namespace demi::runtime::render
