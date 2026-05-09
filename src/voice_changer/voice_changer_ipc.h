/**
 * @file src/voice_changer/voice_changer_ipc.h
 * @brief UDP loopback client for the external voice-changer inference service.
 *
 * Wire protocol v1 (little-endian, fixed 24-byte header + variable PCM payload):
 *   0  u32 magic = 0x56434843 ('VCHC')
 *   4  u8  version = 1
 *   5  u8  msg_type (1=PROCESS_REQ, 2=PROCESS_RSP, 3=PING, 4=PONG)
 *   6  u16 flags (bit0 = passthrough_hint, bit1 = last_in_burst)
 *   8  u32 seq (monotonic; response must echo)
 *  12  u32 sample_rate
 *  16  u16 channels
 *  18  u16 sample_count (per channel)
 *  20  u32 reserved (must be 0)
 *  24  PCM int16 samples, length = sample_count * channels * 2 bytes
 *
 * The mic write thread calls process() per 20 ms frame. On any failure the
 * implementation logs at warning level and leaves the buffer untouched.
 * A short configurable timeout (default 15 ms) keeps the mic path responsive
 * if the inference service stalls or crashes.
 */

#pragma once

#include "voice_changer.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace voice_changer {

  struct ipc_options_t {
    std::string host = "127.0.0.1";
    uint16_t port = 9876;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(15);
  };

  /**
   * @brief Construct the UDP IPC voice-changer client.
   *
   * Returns nullptr on socket-creation failure; callers should fall back to a
   * passthrough implementation in that case.
   */
  std::unique_ptr<voice_changer_t>
  create_ipc(const ipc_options_t &opts);

  // Wire constants exposed for the unit-test sidecar and the reference
  // Python server implementation.
  constexpr uint32_t IPC_MAGIC = 0x56434843u;
  constexpr uint8_t IPC_VERSION = 1u;
  constexpr uint8_t IPC_MSG_PROCESS_REQ = 1u;
  constexpr uint8_t IPC_MSG_PROCESS_RSP = 2u;
  constexpr uint8_t IPC_MSG_PING = 3u;
  constexpr uint8_t IPC_MSG_PONG = 4u;
  constexpr uint16_t IPC_FLAG_PASSTHROUGH_HINT = 0x0001u;
  constexpr size_t IPC_HEADER_SIZE = 24u;

}  // namespace voice_changer
