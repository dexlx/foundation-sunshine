/**
 * @file src/voice_changer/voice_changer.h
 * @brief Voice changer DSP interface for the inbound microphone path.
 *
 * The voice changer sits between OPUS decode and the WASAPI virtual-mic write
 * in src/platform/windows/mic_write.cpp. It receives 48 kHz mono Int16 PCM in
 * 20 ms frames (960 samples) and modifies the buffer in place.
 *
 * PR-A (this file): defines the interface and a passthrough implementation.
 * PR-B: ONNX Runtime backend skeleton.
 * PR-C: Full RVC inference (RMVPE pitch + HuBERT embedding + generator).
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace voice_changer {

  /**
   * @brief Abstract DSP that processes mic PCM in place.
   *
   * Implementations MUST be safe to call from the mic write thread. The caller
   * holds no audio locks during process(); implementations may block briefly
   * but should aim for << 20 ms to stay below one frame of added latency.
   *
   * On any internal failure, implementations should leave the buffer untouched
   * and log at warning level. The caller treats process() as best-effort and
   * always proceeds to write the (possibly unmodified) buffer to the device.
   */
  class voice_changer_t {
  public:
    virtual ~voice_changer_t() = default;

    /**
     * @brief Process one PCM frame in place.
     * @param pcm Pointer to interleaved Int16 PCM samples (currently always mono).
     * @param samples Number of samples (NOT bytes) in the buffer.
     * @param sample_rate Sample rate in Hz; currently always 48000.
     */
    virtual void
    process(int16_t *pcm, size_t samples, int sample_rate) = 0;

    /**
     * @brief Reset internal state (e.g. on session change or buffer underrun).
     */
    virtual void
    reset() = 0;

    /**
     * @brief Backend name for logging. Static lifetime.
     */
    virtual const char *
    name() const = 0;
  };

  /**
   * @brief Construct the voice changer selected by config.
   * @return non-null instance; falls back to passthrough on any backend error.
   *
   * Reads config::audio.voice_changer at construction time. Re-create after a
   * config change to pick up new settings.
   */
  std::unique_ptr<voice_changer_t>
  create();

}  // namespace voice_changer
