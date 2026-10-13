#include "demi/runtime/render/backend/RenderCommands.h"

#include <algorithm>
#include <limits>

namespace demi::runtime::render {

InstanceBatchSubmission
RenderCommands::submitInstanceBatches(const InstancedBufferedDraw &draw,
                                      std::string &error) {
  error.clear();
  const auto stride = draw.instances.stride;
  if (draw.instances.data.empty() || stride == 0 || stride % 16 != 0 ||
      draw.instances.data.size() % stride != 0) {
    error = "Instance batches require complete records with a "
            "sixteen-byte-aligned stride.";
    return {};
  }
  const auto total = draw.instances.data.size() / stride;
  InstanceBatchSubmission result;
  while (result.submittedRecords < total) {
    const auto remaining = total - result.submittedRecords;
    const auto requested = static_cast<std::uint32_t>(
        std::min<std::size_t>(remaining, UINT32_MAX));
    const auto available =
        std::min(availableInstances(stride, requested), requested);
    if (available == 0) {
      error = "Instancing is unsupported or the frame instance-buffer budget "
              "is exhausted.";
      return result;
    }
    auto batch = draw;
    const auto offset = result.submittedRecords * stride;
    const auto bytes = static_cast<std::size_t>(available) * stride;
    batch.instances.data = draw.instances.data.subspan(offset, bytes);
    if (!submit(batch, error)) {
      return result;
    }
    result.submittedRecords += available;
    ++result.submittedBatches;
  }
  result.complete = true;
  return result;
}

} // namespace demi::runtime::render
