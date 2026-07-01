#include "demi/runtime/MediaSystem.h"

#include <algorithm>
#include <iostream>
#include <memory>

#if DEMI_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#endif

namespace demi::runtime {

struct MediaSystem::Playback {
  std::uint64_t handle = 0;
  bool loop = false;
  bool playing = false;
  VideoTexture texture;
#if DEMI_HAS_FFMPEG
  AVFormatContext* format = nullptr;
  AVCodecContext* codec = nullptr;
  AVFrame* frame = nullptr;
  AVPacket* packet = nullptr;
  SwsContext* scaler = nullptr;
  int streamIndex = -1;
#endif
};

namespace {

#if DEMI_HAS_FFMPEG

void closePlayback(MediaSystem::Playback& playback) {
  if (playback.scaler != nullptr) {
    sws_freeContext(playback.scaler);
    playback.scaler = nullptr;
  }
  if (playback.packet != nullptr) {
    av_packet_free(&playback.packet);
  }
  if (playback.frame != nullptr) {
    av_frame_free(&playback.frame);
  }
  if (playback.codec != nullptr) {
    avcodec_free_context(&playback.codec);
  }
  if (playback.format != nullptr) {
    avformat_close_input(&playback.format);
  }
}

bool decodeNextFrame(MediaSystem::Playback& playback) {
  if (playback.format == nullptr || playback.codec == nullptr || playback.frame == nullptr || playback.packet == nullptr) {
    return false;
  }

  while (av_read_frame(playback.format, playback.packet) >= 0) {
    std::unique_ptr<AVPacket, decltype(&av_packet_unref)> packetGuard(playback.packet, av_packet_unref);
    if (playback.packet->stream_index != playback.streamIndex) {
      continue;
    }
    if (avcodec_send_packet(playback.codec, playback.packet) < 0) {
      continue;
    }
    while (avcodec_receive_frame(playback.codec, playback.frame) == 0) {
      const int width = playback.codec->width;
      const int height = playback.codec->height;
      if (width <= 0 || height <= 0) {
        return false;
      }

      playback.texture.width = width;
      playback.texture.height = height;
      playback.texture.rgba.assign(static_cast<std::size_t>(width * height * 4), 0);
      unsigned char* dst[] = {playback.texture.rgba.data()};
      int dstStride[] = {width * 4};
      playback.scaler = sws_getCachedContext(playback.scaler, width, height, playback.codec->pix_fmt, width, height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
      if (playback.scaler == nullptr) {
        return false;
      }
      sws_scale(playback.scaler, playback.frame->data, playback.frame->linesize, 0, height, dst, dstStride);
      return true;
    }
  }

  if (!playback.loop) {
    playback.playing = false;
    return false;
  }

  av_seek_frame(playback.format, playback.streamIndex, 0, AVSEEK_FLAG_BACKWARD);
  avcodec_flush_buffers(playback.codec);
  return decodeNextFrame(playback);
}

std::unique_ptr<MediaSystem::Playback> openPlayback(const std::filesystem::path& path, const std::uint64_t handle, const bool loop) {
  auto playback = std::make_unique<MediaSystem::Playback>();
  playback->handle = handle;
  playback->loop = loop;
  playback->playing = true;

  if (avformat_open_input(&playback->format, path.string().c_str(), nullptr, nullptr) < 0) {
    std::cerr << "FFmpeg failed to open video: " << path.string() << '\n';
    return nullptr;
  }
  if (avformat_find_stream_info(playback->format, nullptr) < 0) {
    std::cerr << "FFmpeg failed to read stream info: " << path.string() << '\n';
    closePlayback(*playback);
    return nullptr;
  }

  const int streamIndex = av_find_best_stream(playback->format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (streamIndex < 0) {
    std::cerr << "FFmpeg video stream not found: " << path.string() << '\n';
    closePlayback(*playback);
    return nullptr;
  }
  playback->streamIndex = streamIndex;

  AVCodecParameters* parameters = playback->format->streams[streamIndex]->codecpar;
  const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
  if (decoder == nullptr) {
    std::cerr << "FFmpeg decoder not found for " << path.string() << '\n';
    closePlayback(*playback);
    return nullptr;
  }
  playback->codec = avcodec_alloc_context3(decoder);
  if (playback->codec == nullptr || avcodec_parameters_to_context(playback->codec, parameters) < 0 || avcodec_open2(playback->codec, decoder, nullptr) < 0) {
    std::cerr << "FFmpeg decoder init failed for " << path.string() << '\n';
    closePlayback(*playback);
    return nullptr;
  }

  playback->frame = av_frame_alloc();
  playback->packet = av_packet_alloc();
  if (playback->frame == nullptr || playback->packet == nullptr || !decodeNextFrame(*playback)) {
    std::cerr << "FFmpeg failed to decode first frame: " << path.string() << '\n';
    closePlayback(*playback);
    return nullptr;
  }

  return playback;
}

#endif

} // namespace

MediaSystem::MediaSystem() = default;

MediaSystem::~MediaSystem() {
  shutdown();
}

void MediaSystem::loadVideoAssets(const AssetRegistry& registry) {
  videos_.clear();
  for (const AssetManifest& asset : registry.assets) {
    if (asset.type == "VideoClip") {
      videos_[asset.id] = asset.sourcePath;
    }
  }
}

std::uint64_t MediaSystem::playVideo(const std::string& assetId, const bool loop) {
#if !DEMI_HAS_FFMPEG
  (void)assetId;
  (void)loop;
  return 0;
#else
  const auto video = videos_.find(assetId);
  if (video == videos_.end()) {
    std::cerr << "Video asset not loaded: " << assetId << '\n';
    return 0;
  }

  const std::uint64_t handle = nextHandle_++;
  std::unique_ptr<Playback> playback = openPlayback(video->second, handle, loop);
  if (!playback) {
    return 0;
  }
  playing_.push_back(playback.release());
  return handle;
#endif
}

bool MediaSystem::stopVideo(const std::uint64_t handle) {
  const auto found = std::ranges::find_if(playing_, [&](const Playback* playback) { return playback != nullptr && playback->handle == handle; });
  if (found == playing_.end()) {
    return false;
  }
#if DEMI_HAS_FFMPEG
  closePlayback(**found);
#endif
  delete *found;
  playing_.erase(found);
  return true;
}

bool MediaSystem::isVideoPlaying(const std::uint64_t handle) const {
  return std::ranges::any_of(playing_, [&](const Playback* playback) { return playback != nullptr && playback->handle == handle && playback->playing; });
}

std::optional<VideoTexture> MediaSystem::videoTexture(const std::uint64_t handle) const {
  const auto found = std::ranges::find_if(playing_, [&](const Playback* playback) { return playback != nullptr && playback->handle == handle; });
  if (found == playing_.end()) {
    return std::nullopt;
  }
  return (*found)->texture;
}

void MediaSystem::update(const float dt) {
  (void)dt;
#if DEMI_HAS_FFMPEG
  for (Playback* playback : playing_) {
    if (playback != nullptr && playback->playing) {
      decodeNextFrame(*playback);
    }
  }
  std::erase_if(playing_, [](Playback* playback) {
    if (playback == nullptr) {
      return true;
    }
    if (playback->playing) {
      return false;
    }
    closePlayback(*playback);
    delete playback;
    return true;
  });
#endif
}

void MediaSystem::shutdown() {
#if DEMI_HAS_FFMPEG
  for (Playback* playback : playing_) {
    if (playback != nullptr) {
      closePlayback(*playback);
      delete playback;
    }
  }
#else
  for (Playback* playback : playing_) {
    delete playback;
  }
#endif
  playing_.clear();
}

} // namespace demi::runtime
