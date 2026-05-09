# Voice Changer Reference Service

Reference implementation of the Sunshine voice-changer IPC backend.

## Quick start

```powershell
python voice_changer_server.py
```

Then in Sunshine config (Web UI → Audio/Video → Voice changer):

- Backend: **External service (UDP loopback)**
- Host: `127.0.0.1`
- Port: `9876`
- Timeout: `15` ms

The default `IdentityBackend` echoes input PCM verbatim; useful to verify
end-to-end wiring before plugging in a real model.

## Wire protocol v1

See [`src/voice_changer/voice_changer_ipc.h`](../../src/voice_changer/voice_changer_ipc.h)
for the canonical definition.

| Offset | Size | Field |
|--------|------|-------|
| 0      | 4    | magic = `0x56434843` (`'VCHC'`) |
| 4      | 1    | version = 1 |
| 5      | 1    | msg_type (1=PROCESS_REQ, 2=PROCESS_RSP, 3=PING, 4=PONG) |
| 6      | 2    | flags |
| 8      | 4    | seq (echo on response) |
| 12     | 4    | sample_rate (Hz) |
| 16     | 2    | channels |
| 18     | 2    | sample_count (per channel) |
| 20     | 4    | reserved (0) |
| 24     | …    | int16 PCM payload |

For Sunshine 20 ms mic frames at 48 kHz mono, payload is 1920 bytes per
packet (1944 bytes including header), well within the IPv4 safe MTU.

## Plugging in a real backend

Subclass `IdentityBackend` and override `process(samples, sample_rate, channels)`.
Return `bytes` of the same length as the input. The current scaffolding
ships no ML dependencies; you can drop in any of:

- [w-okada/voice-changer](https://github.com/w-okada/voice-changer) (RVC, MMVC, …)
- [RVC-Boss/Retrieval-based-Voice-Conversion-WebUI](https://github.com/RVC-Boss/Retrieval-based-Voice-Conversion-WebUI)
- so-vits-svc 4.x
- Any DSP library (sox, librosa) for non-ML effects (pitch shift, reverb).

## Latency budget

The Sunshine client side enforces a per-frame timeout (default 15 ms) and
falls back to passthrough on miss. Keep `process()` well under that bound
or accept occasional dropouts. Loopback UDP RTT is < 1 ms; the rest is
your inference budget.

## Process management

This service does not daemonize. Recommended setups:

- Windows: `Start-Process -WindowStyle Hidden python voice_changer_server.py`
  from a startup script, or wrap with NSSM as a service.
- Linux/macOS: launchd / systemd user unit.

A future PR may add an opt-in helper-process supervisor inside Sunshine
itself (similar to how `vmouse` and `vdd` install/manage their drivers).
