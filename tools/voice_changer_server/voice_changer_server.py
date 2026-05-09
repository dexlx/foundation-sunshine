#!/usr/bin/env python3
"""Minimal voice-changer reference service for Sunshine PR-B.

Implements the wire protocol v1 documented in
`src/voice_changer/voice_changer_ipc.h`. This script ships an *identity*
backend: it echoes the request PCM bytes verbatim. Use it to validate the
end-to-end IPC integration before plugging in a real RVC / so-vits-svc
inference engine.

Usage::

    python voice_changer_server.py [--host 127.0.0.1] [--port 9876]

Drop-in replacement for the inference backend: subclass `IdentityBackend`,
override `process(samples_int16, sample_rate, channels)` to return modified
int16 samples of the same length.

The protocol is intentionally trivial so any language with UDP sockets and
struct packing can speak it.
"""

from __future__ import annotations

import argparse
import logging
import socket
import struct
import sys
from dataclasses import dataclass

LOG = logging.getLogger("voice_changer_server")

# Wire constants — keep in sync with src/voice_changer/voice_changer_ipc.h.
MAGIC = 0x56434843  # 'VCHC'
VERSION = 1
MSG_PROCESS_REQ = 1
MSG_PROCESS_RSP = 2
MSG_PING = 3
MSG_PONG = 4
HEADER_SIZE = 24
HEADER_FMT = "<IBBHIIHHI"  # magic, ver, type, flags, seq, sr, ch, samp, reserved
assert struct.calcsize(HEADER_FMT) == HEADER_SIZE


@dataclass
class Frame:
    msg_type: int
    flags: int
    seq: int
    sample_rate: int
    channels: int
    sample_count: int
    payload: bytes


def decode(buf: bytes) -> Frame | None:
    if len(buf) < HEADER_SIZE:
        return None
    magic, ver, msg, flags, seq, sr, ch, sc, _ = struct.unpack_from(HEADER_FMT, buf, 0)
    if magic != MAGIC or ver != VERSION:
        return None
    payload = buf[HEADER_SIZE:HEADER_SIZE + sc * ch * 2]
    return Frame(msg, flags, seq, sr, ch, sc, payload)


def encode(frame: Frame) -> bytes:
    return (
        struct.pack(
            HEADER_FMT,
            MAGIC,
            VERSION,
            frame.msg_type,
            frame.flags,
            frame.seq,
            frame.sample_rate,
            frame.channels,
            frame.sample_count,
            0,
        )
        + frame.payload
    )


class IdentityBackend:
    """Echo input verbatim. Override `process` to plug in real DSP / ML."""

    name = "identity"

    def process(self, samples: bytes, sample_rate: int, channels: int) -> bytes:
        return samples


def serve(host: str, port: int, backend: IdentityBackend) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
    LOG.info("voice_changer_server listening on udp://%s:%d backend=%s", host, port, backend.name)

    frames_processed = 0
    last_log = 0

    while True:
        try:
            data, addr = sock.recvfrom(8192)
        except KeyboardInterrupt:
            LOG.info("shutting down")
            return
        except OSError as e:
            LOG.warning("recvfrom failed: %s", e)
            continue

        frame = decode(data)
        if frame is None:
            LOG.debug("dropping malformed packet from %s (len=%d)", addr, len(data))
            continue

        if frame.msg_type == MSG_PING:
            reply = encode(Frame(MSG_PONG, 0, frame.seq, frame.sample_rate,
                                 frame.channels, 0, b""))
            sock.sendto(reply, addr)
            continue

        if frame.msg_type != MSG_PROCESS_REQ:
            continue

        try:
            out_payload = backend.process(frame.payload, frame.sample_rate, frame.channels)
        except Exception as e:  # pragma: no cover — defensive
            LOG.exception("backend.process raised; echoing input: %s", e)
            out_payload = frame.payload

        if len(out_payload) != len(frame.payload):
            LOG.warning("backend returned %d bytes, expected %d; truncating/padding",
                        len(out_payload), len(frame.payload))
            if len(out_payload) > len(frame.payload):
                out_payload = out_payload[:len(frame.payload)]
            else:
                out_payload = out_payload + b"\x00" * (len(frame.payload) - len(out_payload))

        reply = encode(Frame(
            MSG_PROCESS_RSP, 0, frame.seq, frame.sample_rate,
            frame.channels, frame.sample_count, out_payload,
        ))
        sock.sendto(reply, addr)

        frames_processed += 1
        if frames_processed - last_log >= 500:
            LOG.info("processed %d frames", frames_processed)
            last_log = frames_processed


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=9876)
    p.add_argument("--log-level", default="INFO")
    args = p.parse_args(argv)

    logging.basicConfig(
        level=getattr(logging, args.log_level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )

    serve(args.host, args.port, IdentityBackend())
    return 0


if __name__ == "__main__":
    sys.exit(main())
