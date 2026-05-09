/**
 * @file src/voice_changer/voice_changer_ipc.cpp
 * @brief Synchronous UDP loopback client for the external voice-changer service.
 *
 * One socket per instance. process() is request-response, blocking up to the
 * configured timeout (default 15 ms). On any error or timeout the input PCM
 * is left untouched and a warning is logged. WSAStartup is reference-counted
 * so multiple voice-changer instances and other Sunshine subsystems coexist.
 */

#include "voice_changer_ipc.h"

#include "src/logging.h"

// Winsock includes must precede windows.h consumers.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace voice_changer {

  namespace {

    // Reference-counted WSAStartup so we don't fight other Sunshine modules
    // that already brought winsock up. Pairs WSAStartup with WSACleanup on
    // the last drop.
    class wsa_guard_t {
    public:
      static bool
      acquire() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_refcount == 0) {
          WSADATA data;
          int rc = WSAStartup(MAKEWORD(2, 2), &data);
          if (rc != 0) {
            BOOST_LOG(error) << "voice_changer_ipc: WSAStartup failed: " << rc;
            return false;
          }
        }
        ++s_refcount;
        return true;
      }

      static void
      release() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_refcount > 0 && --s_refcount == 0) {
          WSACleanup();
        }
      }

    private:
      static std::mutex s_mutex;
      static int s_refcount;
    };

    std::mutex wsa_guard_t::s_mutex;
    int wsa_guard_t::s_refcount = 0;

    /**
     * @brief Encode the 24-byte IPC v1 header at the start of buf.
     */
    void
    encode_header(uint8_t *buf, uint8_t msg_type, uint16_t flags, uint32_t seq,
                  uint32_t sample_rate, uint16_t channels, uint16_t sample_count) {
      const uint32_t magic = IPC_MAGIC;
      std::memcpy(buf + 0, &magic, 4);
      buf[4] = IPC_VERSION;
      buf[5] = msg_type;
      std::memcpy(buf + 6, &flags, 2);
      std::memcpy(buf + 8, &seq, 4);
      std::memcpy(buf + 12, &sample_rate, 4);
      std::memcpy(buf + 16, &channels, 2);
      std::memcpy(buf + 18, &sample_count, 2);
      const uint32_t reserved = 0;
      std::memcpy(buf + 20, &reserved, 4);
    }

    class ipc_changer_t: public voice_changer_t {
    public:
      explicit ipc_changer_t(const ipc_options_t &opts) : opts_(opts) {}

      ~ipc_changer_t() override {
        if (sock_ != INVALID_SOCKET) {
          closesocket(sock_);
          sock_ = INVALID_SOCKET;
        }
        if (wsa_acquired_) {
          wsa_guard_t::release();
        }
      }

      bool
      init() {
        if (!wsa_guard_t::acquire()) {
          return false;
        }
        wsa_acquired_ = true;

        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ == INVALID_SOCKET) {
          BOOST_LOG(error) << "voice_changer_ipc: socket() failed: " << WSAGetLastError();
          return false;
        }

        // Resolve target address once; loopback only.
        std::memset(&peer_, 0, sizeof(peer_));
        peer_.sin_family = AF_INET;
        peer_.sin_port = htons(opts_.port);
        if (InetPtonA(AF_INET, opts_.host.c_str(), &peer_.sin_addr) != 1) {
          BOOST_LOG(error) << "voice_changer_ipc: invalid host '" << opts_.host << "'";
          return false;
        }

        // Recv timeout via SO_RCVTIMEO (millisecond DWORD on Windows).
        DWORD ms = static_cast<DWORD>(opts_.timeout.count());
        if (setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char *>(&ms), sizeof(ms)) == SOCKET_ERROR) {
          BOOST_LOG(warning) << "voice_changer_ipc: SO_RCVTIMEO failed: " << WSAGetLastError();
          // Non-fatal; recvfrom will block longer but still works.
        }

        BOOST_LOG(info) << "voice_changer_ipc: ready, peer=" << opts_.host << ":" << opts_.port
                        << " timeout=" << opts_.timeout.count() << "ms";
        return true;
      }

      void
      process(int16_t *pcm, size_t samples, int sample_rate) override {
        if (sock_ == INVALID_SOCKET || pcm == nullptr || samples == 0) {
          return;
        }
        // Sanity caps: refuse anything that would overflow the wire format
        // (sample_count is u16) or blow past safe UDP MTU. 20 ms @ 48 kHz mono
        // is 960 samples / 1944 bytes, well within both bounds.
        if (samples > 0xFFFFu || samples * 2 + IPC_HEADER_SIZE > MAX_DATAGRAM) {
          BOOST_LOG(warning) << "voice_changer_ipc: oversize frame samples=" << samples;
          return;
        }

        const size_t payload_bytes = samples * sizeof(int16_t);
        const size_t total = IPC_HEADER_SIZE + payload_bytes;
        tx_buf_.resize(total);
        rx_buf_.resize(total);

        const uint32_t seq = ++last_seq_;
        encode_header(tx_buf_.data(), IPC_MSG_PROCESS_REQ, 0, seq,
                      static_cast<uint32_t>(sample_rate), 1,
                      static_cast<uint16_t>(samples));
        std::memcpy(tx_buf_.data() + IPC_HEADER_SIZE, pcm, payload_bytes);

        int sent = sendto(sock_, reinterpret_cast<const char *>(tx_buf_.data()),
                          static_cast<int>(total), 0,
                          reinterpret_cast<const sockaddr *>(&peer_), sizeof(peer_));
        if (sent != static_cast<int>(total)) {
          handle_failure("sendto", WSAGetLastError());
          return;
        }

        // Drain at most a couple of stale responses to find one matching seq.
        // In the common case the first packet is already correct.
        for (int attempt = 0; attempt < 3; ++attempt) {
          int n = recvfrom(sock_, reinterpret_cast<char *>(rx_buf_.data()),
                           static_cast<int>(rx_buf_.size()), 0, nullptr, nullptr);
          if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
              ++timeout_count_;
              maybe_log_timeout();
              return;  // leave pcm untouched
            }
            handle_failure("recvfrom", err);
            return;
          }

          if (static_cast<size_t>(n) < IPC_HEADER_SIZE) {
            continue;
          }
          uint32_t magic;
          std::memcpy(&magic, rx_buf_.data() + 0, 4);
          if (magic != IPC_MAGIC) continue;
          if (rx_buf_[4] != IPC_VERSION) continue;
          if (rx_buf_[5] != IPC_MSG_PROCESS_RSP) continue;
          uint32_t resp_seq;
          std::memcpy(&resp_seq, rx_buf_.data() + 8, 4);
          if (resp_seq != seq) continue;  // stale

          uint16_t resp_samples;
          std::memcpy(&resp_samples, rx_buf_.data() + 18, 2);
          if (resp_samples != samples) {
            BOOST_LOG(warning) << "voice_changer_ipc: response sample count mismatch (got "
                               << resp_samples << " expected " << samples << ")";
            return;
          }
          if (static_cast<size_t>(n) < IPC_HEADER_SIZE + payload_bytes) {
            BOOST_LOG(warning) << "voice_changer_ipc: short response payload";
            return;
          }
          std::memcpy(pcm, rx_buf_.data() + IPC_HEADER_SIZE, payload_bytes);
          ++ok_count_;
          return;
        }
        // Three stale packets in a row — give up this frame, keep input.
      }

      void
      reset() override {
        last_seq_ = 0;
      }

      const char *
      name() const override {
        return "ipc";
      }

    private:
      static constexpr size_t MAX_DATAGRAM = 8192;  // worst case: small mtu + headroom for future formats

      void
      handle_failure(const char *op, int err) {
        // Throttle spammy logs: one per 100 failures.
        if (++fail_count_ % 100 == 1) {
          BOOST_LOG(warning) << "voice_changer_ipc: " << op << " failed (" << err
                             << "); falling back to passthrough for this frame (count="
                             << fail_count_ << ")";
        }
      }

      void
      maybe_log_timeout() {
        // Same throttle as handle_failure to avoid log floods if the service is down.
        if (timeout_count_ % 100 == 1) {
          BOOST_LOG(debug) << "voice_changer_ipc: timeout waiting for response (count="
                           << timeout_count_ << ")";
        }
      }

      ipc_options_t opts_;
      SOCKET sock_ = INVALID_SOCKET;
      sockaddr_in peer_ {};
      bool wsa_acquired_ = false;

      std::vector<uint8_t> tx_buf_;
      std::vector<uint8_t> rx_buf_;

      uint32_t last_seq_ = 0;
      uint64_t ok_count_ = 0;
      uint64_t fail_count_ = 0;
      uint64_t timeout_count_ = 0;
    };

  }  // namespace

  std::unique_ptr<voice_changer_t>
  create_ipc(const ipc_options_t &opts) {
    auto ipc = std::make_unique<ipc_changer_t>(opts);
    if (!ipc->init()) {
      return nullptr;
    }
    return ipc;
  }

}  // namespace voice_changer
