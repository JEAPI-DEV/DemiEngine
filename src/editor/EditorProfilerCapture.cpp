#include "editor/EditorProfilerCapture.h"
#include "editor/EditorProfilerGameView.h"

#include <span>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace demi::editor {
namespace {

using Frame = runtime::RuntimeProfiler::FrameCapture;

double timelineExtent(
    std::span<const runtime::RuntimeProfiler::ScopeEvent> events) {
  double extent = 0.01;
  for (const auto &event : events) {
    extent = std::max(extent, event.startMilliseconds + event.durationMilliseconds);
  }
  return extent;
}

void drawScaleControls(const char *label, EditorProfilerScale &scale) {
  ImGui::PushID(label);
  bool automatic = scale.automatic();
  if (ImGui::Checkbox("Auto fit", &automatic)) {
    scale.setAutomatic(automatic);
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(95);
  float milliseconds = static_cast<float>(scale.span());
  ImGui::BeginDisabled(automatic);
  if (ImGui::InputFloat(label, &milliseconds, 0, 0, "%.2f ms")) {
    scale.setManualSpan(milliseconds);
  }
  ImGui::EndDisabled();
  ImGui::PopID();
}

void drawTimeline(std::span<const runtime::RuntimeProfiler::ScopeEvent> events,
                  bool gpu, float zoom, double extent) {
  if (events.empty()) {
    ImGui::TextDisabled(gpu ? "No GPU timing sample received in this frame."
                            : "No instrumented CPU scopes in this frame.");
    return;
  }
  std::size_t maximumDepth = 0;
  std::size_t overflowEvents = 0;
  for (const auto &event : events) {
    maximumDepth = std::max(maximumDepth, event.depth);
    if (event.startMilliseconds + event.durationMilliseconds > extent) {
      ++overflowEvents;
    }
  }
  const double actualExtent = timelineExtent(events);
  if (actualExtent > extent) {
    ImGui::TextColored({1.0F, 0.65F, 0.25F, 1.0F},
                        "Beyond manual axis: %.3f ms extent; %zu clipped events. Enable Auto fit.",
                        actualExtent, overflowEvents);
  }
  ImGui::BeginChild("scope-timeline", {0, 160}, ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_HorizontalScrollbar);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float width = std::max(200.0F, ImGui::GetContentRegionAvail().x) * zoom;
  constexpr float RowHeight = 24.0F;
  const float height = RowHeight * static_cast<float>(maximumDepth + 2);
  ImGui::InvisibleButton("timeline-area", {width, height});
  auto *draw = ImGui::GetWindowDrawList();
  for (int tick = 0; tick <= 8; ++tick) {
    const float x = origin.x + width * static_cast<float>(tick) / 8.0F;
    draw->AddLine({x, origin.y}, {x, origin.y + height},
                  IM_COL32(95, 105, 115, 80));
    char label[32];
    std::snprintf(label, sizeof(label), "%.2f ms", extent * tick / 8.0);
    draw->AddText({x + 2, origin.y}, IM_COL32(170, 180, 190, 255), label);
  }
  for (const auto &event : events) {
    const double eventEnd = event.startMilliseconds + event.durationMilliseconds;
    if (event.startMilliseconds >= extent || eventEnd <= 0.0) {
      continue;
    }
    const float x = origin.x + width * static_cast<float>(
        std::max(0.0, event.startMilliseconds) / extent);
    const float right = origin.x + width * static_cast<float>(
        std::min(extent, eventEnd) / extent);
    const float y = origin.y + RowHeight * static_cast<float>(event.depth + 1);
    const ImVec2 end{std::max(x + 1.0F, right), y + RowHeight - 3};
    const auto hash = std::hash<std::string>{}(event.name);
    const ImU32 color =
        ImColor::HSV(static_cast<float>(hash % 360) / 360.0F, 0.55F, 0.65F);
    draw->AddRectFilled({x, y}, end, color, 2);
    if (eventEnd > extent) {
      draw->AddTriangleFilled({end.x - 7, y + 3},
                              {end.x - 7, end.y - 3},
                              {end.x, (y + end.y) * 0.5F},
                              IM_COL32(255, 190, 70, 255));
    }
    draw->PushClipRect({x, y}, end, true);
    draw->AddText({x + 3, y + 2}, IM_COL32_WHITE, event.name.c_str());
    draw->PopClipRect();
    if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect({x, y}, end)) {
      ImGui::SetTooltip("%s\nStart %.3f ms\nDuration %.3f ms (inclusive)",
                        event.name.c_str(), event.startMilliseconds,
                        event.durationMilliseconds);
    }
  }
  ImGui::EndChild();
}

} // namespace

const runtime::RuntimeProfiler::FrameCapture *EditorProfilerCapture::draw() {
  using Profiler = runtime::RuntimeProfiler;
  if (generation_ != Profiler::sessionGeneration()) {
    generation_ = Profiler::sessionGeneration();
    frozen_.clear();
    liveDetail_.reset();
    projectedDetail_.reset();
    gameScopes_.clear();
    lastDetailRefresh_ = -1.0;
    cpuScale_.resetSession(16.67);
    gpuScale_.resetSession(16.67);
    historyScale_.resetSession(33.33);
    inspecting_ = false;
  }
  const auto freeze = [&] {
    frozen_ = Profiler::captures();
    selected_ = static_cast<int>(frozen_.size()) - 1;
    if (liveDetail_) {
      for (std::size_t index = 0; index < frozen_.size(); ++index) {
        if (frozen_[index] == liveDetail_) {
          selected_ = static_cast<int>(index);
          break;
        }
      }
    }
    inspecting_ = !frozen_.empty();
  };
  if (ImGui::Button(inspecting_ ? "Resume capture view"
                                : "Pause capture view")) {
    if (inspecting_) {
      inspecting_ = false;
      frozen_.clear();
      lastDetailRefresh_ = -1.0;
    } else {
      freeze();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Previous")) {
    if (!inspecting_) {
      freeze();
    }
    selected_ = std::max(0, selected_ - 1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Next") && inspecting_) {
    selected_ = std::min(static_cast<int>(frozen_.size()) - 1, selected_ + 1);
  }
  const auto &history = inspecting_ ? frozen_ : Profiler::captures();
  if (history.empty()) {
    ImGui::TextDisabled("Waiting for a completed frame.");
    return nullptr;
  }
  if (!inspecting_) {
    selected_ = static_cast<int>(history.size()) - 1;
  }
  historyTimes_.clear();
  historyTimes_.reserve(history.size());
  std::size_t overflowFrames = 0;
  double maximumInterval = 0.0;
  for (const auto &frame : history) {
    const double gameMilliseconds = editorGameCpuMilliseconds(*frame);
    historyTimes_.push_back(static_cast<float>(gameMilliseconds));
    maximumInterval = std::max(maximumInterval, gameMilliseconds);
  }
  historyScale_.observe(maximumInterval, ImGui::GetTime());
  drawScaleControls("History axis", historyScale_);
  // Enabling Auto fit should include the current raw range immediately.
  historyScale_.observe(maximumInterval, ImGui::GetTime());
  const float historySpanMilliseconds = static_cast<float>(historyScale_.span());
  for (const auto interval : historyTimes_) {
    if (interval > historySpanMilliseconds) {
      ++overflowFrames;
    }
  }
  ImGui::PlotLines(
      "##frame-history", historyTimes_.data(), static_cast<int>(historyTimes_.size()), 0,
      "Game CPU work: update + render submission", 0.0F,
      historySpanMilliseconds, {-1, 64});
  const ImVec2 plotMinimum = ImGui::GetItemRectMin();
  const ImVec2 plotSize = ImGui::GetItemRectSize();
  for (std::size_t index = 0; index < historyTimes_.size(); ++index) {
    if (historyTimes_[index] > historySpanMilliseconds) {
      const float x = plotMinimum.x + 2.0F + (plotSize.x - 4.0F) *
          static_cast<float>(index) /
          static_cast<float>(std::max<std::size_t>(1, historyTimes_.size() - 1));
      const float y = plotMinimum.y + 2.0F;
      ImGui::GetWindowDrawList()->AddTriangleFilled(
          {x - 3, y}, {x + 3, y}, {x, y + 6}, IM_COL32(255, 190, 70, 255));
    }
  }
  if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const float fraction =
        std::clamp((ImGui::GetMousePos().x - ImGui::GetItemRectMin().x) /
                       std::max(1.0F, ImGui::GetItemRectSize().x),
                   0.0F, 1.0F);
    if (!inspecting_) {
      freeze();
    }
    selected_ = std::min(
        static_cast<int>(frozen_.size()) - 1,
        static_cast<int>(fraction * static_cast<float>(frozen_.size())));
  }
  if (overflowFrames > 0) {
    ImGui::TextColored({1.0F, 0.65F, 0.25F, 1.0F},
                        "%zu game samples exceed history axis; maximum %.3f ms.",
                        overflowFrames, maximumInterval);
  }
  // Holding an immutable detail pointer never changes or smooths raw history.
  const auto &selectedHistory = inspecting_ ? frozen_ : Profiler::captures();
  if (!inspecting_) {
    const double now = ImGui::GetTime();
    if (!liveDetail_ || lastDetailRefresh_ < 0.0 ||
        now - lastDetailRefresh_ >= 0.25) {
      liveDetail_ = selectedHistory.back();
      lastDetailRefresh_ = now;
    }
  }
  const auto detail = inspecting_
      ? selectedHistory[static_cast<std::size_t>(selected_)] : liveDetail_;
  const Frame &frame = *detail;
  if (projectedDetail_ != detail) {
    gameScopes_ = editorGameCpuScopes(frame);
    projectedDetail_ = detail;
  }
  ImGui::Text("Frame %zu | Game CPU %.3f ms | %zu retained%s", frame.number,
              editorGameCpuMilliseconds(frame), selectedHistory.size(),
              inspecting_ ? " | inspection paused; game continues" : "");
  const auto gpuTotal = std::ranges::find(
      frame.entries, "GPU.game_view", &Profiler::Entry::name);
  if (!frame.gpuScopes.empty() && gpuTotal != frame.entries.end()) {
    ImGui::Text("Game GPU passes %.3f ms (delayed sample; independent clock)",
                gpuTotal->totalMilliseconds);
  } else {
    ImGui::TextDisabled("Game GPU timing unavailable for this capture.");
  }
  if (ImGui::RadioButton("Game CPU scopes", !gpu_)) {
    gpu_ = false;
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("GPU passes", gpu_)) {
    gpu_ = true;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150);
  ImGui::SliderFloat("Zoom", &zoom_, 1, 16, "%.1fx");
  ImGui::TextDisabled(gpu_ ? "Delayed GPU sample; independent clock, not "
                             "aligned to this CPU frame."
                           : "Game scopes only; editor/backend work excluded. "
                             "Starts/depths normalized; retained gaps remain real.");
  if (!gameScopes_.empty()) {
    cpuScale_.observe(timelineExtent(gameScopes_), ImGui::GetTime());
  }
  if (!frame.gpuScopes.empty()) {
    gpuScale_.observe(timelineExtent(frame.gpuScopes), ImGui::GetTime());
  }
  auto &timelineScale = gpu_ ? gpuScale_ : cpuScale_;
  drawScaleControls(gpu_ ? "GPU axis" : "CPU axis", timelineScale);
  const std::span<const Profiler::ScopeEvent> events =
      gpu_ ? std::span<const Profiler::ScopeEvent>(frame.gpuScopes)
           : std::span<const Profiler::ScopeEvent>(gameScopes_);
  if (!events.empty()) {
    timelineScale.observe(timelineExtent(events), ImGui::GetTime());
  }
  ImGui::TextWrapped("Game CPU history updates every frame; details refresh at 4 Hz. "
                     "Auto fit expands immediately and shrinks after 2 seconds "
                     "of lower values. CPU, GPU and history scales are independent; "
                     "measurements are never smoothed.");
  drawTimeline(events, gpu_, zoom_, timelineScale.span());
  if (frame.droppedScopes > 0) {
    ImGui::TextDisabled("Trace limit reached: %zu scope events omitted.",
                        frame.droppedScopes);
  }
  return &frame;
}

} // namespace demi::editor
