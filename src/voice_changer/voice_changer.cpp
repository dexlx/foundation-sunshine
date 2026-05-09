/**
 * @file src/voice_changer/voice_changer.cpp
 * @brief Voice changer factory and passthrough implementation (PR-A scaffold).
 *
 * This file intentionally has zero ML dependencies. It validates the
 * config -> factory -> mic_write hook integration end to end before PR-B
 * adds ONNX Runtime.
 */

#include "voice_changer.h"

#include "src/config.h"
#include "src/logging.h"

namespace voice_changer {

  namespace {

    /**
     * @brief No-op implementation; returned when the feature is disabled or as
     *        a safe fallback when a real backend fails to initialize.
     */
    class passthrough_t: public voice_changer_t {
    public:
      void
      process(int16_t * /*pcm*/, size_t /*samples*/, int /*sample_rate*/) override {
        // Intentionally empty; preserves input bit-exact.
      }

      void
      reset() override {
        // No state.
      }

      const char *
      name() const override {
        return "passthrough";
      }
    };

  }  // namespace

  std::unique_ptr<voice_changer_t>
  create() {
    const auto &cfg = config::audio.voice_changer;

    if (!cfg.enabled) {
      BOOST_LOG(debug) << "Voice changer disabled; using passthrough";
      return std::make_unique<passthrough_t>();
    }

    switch (cfg.backend) {
      case config::VOICE_CHANGER_BACKEND_PASSTHROUGH:
        BOOST_LOG(info) << "Voice changer backend: passthrough (explicitly selected)";
        return std::make_unique<passthrough_t>();

      case config::VOICE_CHANGER_BACKEND_ONNX:
        // PR-B will replace this branch with the ONNX Runtime backend.
        BOOST_LOG(warning) << "Voice changer ONNX backend not implemented yet (PR-B); falling back to passthrough";
        return std::make_unique<passthrough_t>();

      default:
        BOOST_LOG(warning) << "Unknown voice changer backend " << cfg.backend << "; falling back to passthrough";
        return std::make_unique<passthrough_t>();
    }
  }

}  // namespace voice_changer
