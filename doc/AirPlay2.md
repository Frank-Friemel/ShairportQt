# AirPlay 2 receiver – implementation notes

This document describes the minimal AirPlay 2 (AP2) audio receiver added to ShairportQt to resolve
[#33](https://github.com/Frank-Friemel/ShairportQt/issues/33) (the iPhone disconnects during a phone call). In AirPlay 2
"buffered audio" mode, iOS keeps the session alive across interruptions such as calls, Siri or notifications.

AirPlay 1 (RAOP) still works unchanged. Both protocols are served on the same RTSP port, and each request is routed by its
method, path and content type.

## Status

| Area | State |
|------|-------|
| Discovery (`_airplay._tcp` + extended `_raop._tcp`) | ✅ |
| `GET /info` (binary plist) | ✅ |
| Transient pair-setup (SRP-6a, 3072 bit, SHA-512, PIN `3939`) | ✅ |
| Pair-verify (X25519 + Ed25519 signature of the receiver) | ✅ (the controller's signature isn't checked, see optional features) |
| Encrypted RTSP control channel (ChaCha20-Poly1305 framing) | ✅ |
| SETUP phase 1 (event channel, PTP timing announcement) | ✅ |
| SETUP phase 2: buffered audio (type 103), TCP | ✅ ALAC and AAC-LC, 44.1 and 48 kHz |
| SETUP phase 2: realtime audio (type 96), UDP | ✅ basic (no retransmission requests) |
| RECORD, SETRATEANCHORTIME, FLUSHBUFFERED, TEARDOWN (stream / session) | ✅ |
| SET_PARAMETER volume, DMAP/artwork metadata, progress | ✅ (shared with AirPlay 1) |
| DACP remote control (play/pause/next... from the UI / SMTC / MPRIS) | ✅ (via `DACP-ID`/`Active-Remote` headers) |
| PTP timing | ⚠️ passive slave on UDP 319/320. If the ports can't be bound, it falls back to local timing |
| Password protection | ❌ AP2 is disabled while a password is set (only AP1 is advertised) |
| HomeKit pairing, multi-room, video, mirroring… | ❌ see [AirPlay2-OptionalFeatures.md](AirPlay2-OptionalFeatures.md) |

AP2 is **off by default** and marked *experimental*. Enable it with the "AirPlay 2 (experimental)" checkbox in the options
dialog or the `EnableAirPlay2` config value. It is only available when the build found FFmpeg.

## Dependencies

- **OpenSSL ≥ 3** (already required): Ed25519, X25519, ChaCha20-Poly1305, HKDF-SHA512, SHA-512 and the SRP big-number
  math.
- **FFmpeg** (`libavcodec`, `libavutil`, `libswresample`): decodes ALAC and AAC and resamples 48 kHz to the 44.1 kHz
  output pipeline. CMake option `SHAIRPORT_WITH_FFMPEG` (default `ON`). It uses `pkg-config` on Unix and
  `find_package(FFMPEG)` (vcpkg: `ffmpeg[avcodec,swresample]`) on Windows. Without FFmpeg, ShairportQt builds and
  behaves exactly as before: AP2 is unavailable.
- No other new libraries. TLV8, binary plists, SRP and the PTP parser are implemented in `lib/airplay2/`.

## Architecture

```
                       ┌───────────────────────── RaopServer (httplib, RTSP port 5000+) ─────────────────────────┐
 RTSP request ───────▶ │ per-connection state (ConnectionState in httplib connection context)                    │
                       │   ├─ AirPlay2::Service::IsAirPlay2Request()? ──yes──▶ AirPlay2::Service::Handle()       │
                       │   │                                                     ├─ Pairing (pair-setup/verify)  │
                       │   │                                                     ├─ /info, SETUP, RECORD, ...    │
                       │   │                                                     └─ creates Ap2AudioSession      │
                       │   └─ no ──▶ existing AirPlay 1 handlers (ANNOUNCE/SETUP/RECORD/... → HairTunes)          │
                       │ shared: SET_PARAMETER (volume/metadata/artwork), GET_PARAMETER, DACP                    │
                       └──────────────────────────────────────────────────────────────────────────────────────────┘
```

- `inc/IAudioSession.h`: the common interface of `HairTunes` (AP1) and `Ap2AudioSession` (AP2). `RaopServer` keeps one
  current session and uses it for volume, flush, progress and "is playing".
- `inc/httplib/httplib_raop.h`: the httplib patch now has a per-connection context (`user_data`) and a
  `pending_filter`. After pair-setup/pair-verify succeeds, the response is still sent in plain text, and every following
  byte in both directions goes through `EncryptedFilter` (`SecureChannel`).
- `AirPlay2Handler` (`Service` + `Connection`): the AP2 RTSP logic, the device identity (Ed25519 key `AirPlay2Key`, `pi`,
  `psi` stored in the config), the feature flags and the TXT records.
- `Ap2AudioSession`: data and control sockets, decryption, a jitter queue, anchor/PTP-based start, decoding through
  `Ap2Decoder`, and output into the existing ALSA / WASAPI pipe (`BlobStream` + `AlsaAudio::Play`).
- `PtpClock`: a passive PTP (IEEE 1588v2) listener. It parses Sync/Follow_Up and Announce messages and keeps a
  per-clock-ID offset to the local monotonic clock.

### Feature flags

`features = 0x18340405C4A00` plus bits 15/16/17 (metadata: text, artwork, progress) unless `NoMetaInfo` is set.
The bits that are set are 9, 11, 14, 18, 19, 20, 22, 30, 38, 40, 41, 47 and 48. Among them are AirPlay audio (9), the audio formats
(18–20), buffered audio (38), PTP (40) and the system/CoreUtils pairing bits (47/48). For the meaning of every bit see the
[openairplay feature table](https://openairplay.github.io/airplay-spec/features.html) and the
[shairport-sync AirPlay 2 notes](https://github.com/mikebrady/shairport-sync/blob/master/AIRPLAY2.md).
It's the value shairport-sync uses, minus bit 50 (metadata as binary plist, see the optional features), which current iOS and macOS senders accept
when it comes from shairport-sync.

## Testing

- Unit tests (`ShairportQtTest`) cover: TLV8, binary plist, SRP, transient pairing with a client, a wrong PIN,
  pair-verify, PTP parsing and offsets, audio packet decryption, the format tables, an ALAC decode (bit-exact) and
  `/info` with the TXT records.
- Manual end-to-end: `ShairportQtTest --gtest_also_run_disabled_tests --gtest_filter=*ServeForManualTesting*` runs a
  real `RaopServer` with AP2 enabled for `AP2_SERVE_SECONDS` (default 30) seconds. You can connect a real iPhone
  (if mDNS works) or a scripted client to it. `test/tools/ap2_client.py` is an independent Python sender that goes through `/info`, transient
  pair-setup, encrypted SETUP ×2, RECORD, SETRATEANCHORTIME, buffered ALAC audio, FLUSHBUFFERED and TEARDOWN
  against this harness (verified during development; the ALSA output needs a sound device).
- **Not yet verified:** a real iOS / macOS sender, and the Windows build (FFmpeg through vcpkg). Please report results.

## Troubleshooting

- Enable "Log to file" (or start with `-log`). The AP2 messages are prefixed with `AirPlay2:`.
- `can't bind PTP ports 319/320`: see the PTP section in the [optional features](AirPlay2-OptionalFeatures.md#ptp-timing).
  Playback still works, but the start time isn't synchronized.
- iOS doesn't show the AirPlay 2 badge / still uses AP1: check that `_airplay._tcp` is published
  (`avahi-browse -r _airplay._tcp`) and that no password is set.
