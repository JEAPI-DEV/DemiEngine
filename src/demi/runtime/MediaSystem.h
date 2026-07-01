#pragma once

#include "demi/assets/AssetRegistry.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct VideoTexture {
  int width = 0;
  int height = 0;
  // Invalid bgfx texture handle until the renderer uploads rgba into bgfx.
  std::uint16_t bgfxHandle = 0xffff;
  std::vector<unsigned char> rgba;
};

class MediaSystem {
public:
  struct Playback;

  MediaSystem();
  ~MediaSystem();

  MediaSystem(const MediaSystem&) = delete;
  MediaSystem& operator=(const MediaSystem&) = delete;

  void loadVideoAssets(const AssetRegistry& registry);
  [[nodiscard]] std::uint64_t playVideo(const std::string& assetId, bool loop = false);
  [[nodiscard]] bool stopVideo(std::uint64_t handle);
  [[nodiscard]] bool isVideoPlaying(std::uint64_t handle) const;
  [[nodiscard]] std::optional<VideoTexture> videoTexture(std::uint64_t handle) const;
  void update(float dt);
  void shutdown();

private:
  std::unordered_map<std::string, std::filesystem::path> videos_;
  std::vector<Playback*> playing_;
  std::uint64_t nextHandle_ = 1;
};

} // namespace demi::runtime
