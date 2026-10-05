# AirPlay 2 – optional features (not implemented yet)

This is a backlog of everything that could be added on top of the minimal AirPlay 2 receiver described in
[AirPlay2.md](AirPlay2.md), with an implementation sketch for each item. The estimates are rough person-week ranges for
one developer who already knows this code base. "Reference" points to open-source implementations worth reading.
Note that shairport-sync and UxPlay are GPL, the same license family as ShairportQt.

| # | Feature | Value | Effort |
|---|---------|-------|--------|
| 1 | [PTP timing robustness](#ptp-timing) | high (sync quality, multi-room prerequisite) | 1–3 w |
| 2 | [Clock drift compensation / resampling](#drift-compensation) | high for long sessions | 1–2 w |
| 3 | [Password / PIN for AirPlay 2](#password-and-pin) | medium | 1–2 w |
| 4 | [HomeKit pairing & persistent controllers](#homekit-pairing) | medium (Home app, "Access control") | 2–3 w |
| 5 | [Realtime stream retransmissions](#realtime-retransmissions) | low–medium | 0.5–1 w |
| 6 | [More audio formats (PCM, AAC-ELD, Opus, 24 bit / hi-res)](#audio-formats) | low–medium | 0.5–1.5 w |
| 7 | [Remote control over the event channel (MediaRemote)](#remote-control) | medium | 2–3 w |
| 8 | [Binary-plist metadata (feature bit 50)](#bplist-metadata) | low | 0.5–1 w |
| 9 | [Multi-room / SETPEERS / group handling](#multi-room) | medium | 2–4 w |
| 10 | [Volume / device-side controls (SETRATE, audioMode, …)](#volume-and-misc-commands) | low | 0.5 w |
| 11 | [Photos](#photos) | low | 1–2 w |
| 12 | [Video / URL casting ("AirPlay video")](#video-url-casting) | medium | 3–5 w |
| 13 | [Screen mirroring](#screen-mirroring) | high but very costly | 6–10 w (+ FairPlay risk) |
| 14 | [Windows-specific work](#windows) | required for Windows releases | 0.5–1 w |
| 15 | [Packaging, CI & interop testing](#packaging-and-testing) | required before "non-experimental" | 1 w |

---

## PTP timing

**Today:** `PtpClock` binds UDP 319/320 and passively listens for Sync/Follow_Up messages from the sender (the
grandmaster). It estimates `offset = networkTime − localTime` with a min-filter. If the ports can't be bound (they are
privileged on Linux, or another PTP daemon such as `nqptp` uses them), playback starts at the time of the anchor
request ("local timing"). This is fine for a single speaker, but it isn't sample-synchronized with other speakers or video.

**How to improve:**
1. *Privileges:* document/automate `setcap cap_net_bind_service=+ep` (Debian `postinst`), or ship a systemd socket unit.
   On Windows no privileges are needed, but the firewall must allow UDP 319/320.
2. *Coexistence with nqptp:* if binding fails, try to attach to a running `nqptp` (shairport-sync's daemon). It publishes
   the master clock in the shared-memory segment `/nqptp` (struct `shm_structure`, version-checked) and accepts control
   messages on UDP 9000 (`"<shm name> T <ip list>"` to set the timing peers). That gives exact timing without ports.
3. *Active participation:* some senders only send Sync messages to peers that are listed in the `timingPeerInfo` / SETPEERS
   address list, or after a Delay_Req. Implement Delay_Req/Delay_Resp (end-to-end delay mechanism) and Announce
   handling with the Best Master Clock algorithm. The sender then measures the path delay correctly. Reference:
   `nqptp` (GPL-2), `linuxptp`.
4. Feed SETPEERS (see multi-room) into the PTP peer filter, so only Sync messages from group members are used.

Tests: replay captured PTP packets (pcap → unit test), and check the offset filter with a simulated drift and jitter.

## Drift compensation

**Today:** the start time is aligned to the anchor. After that, audio is played at the sound card's rate. Over a long
session the sender's clock (PTP) and the sound card drift apart (typically 10–100 ppm, i.e. 0.6–6 ms per minute).
With buffered audio the receiver holds several seconds of data, so the queue absorbs this. Lip-sync with video and
multi-room accuracy degrade slowly, though.

**How to improve:**
- Measure the error periodically: `expectedPlayTime(frame) = anchorLocal + (rtp − anchorRtp)/rate` versus the real output
  position (pipe fill level + device latency; `snd_pcm_delay` on ALSA, `IAudioClock::GetPosition` on WASAPI).
- Correct small errors by inserting or dropping single frames (simplest, shairport-sync's "basic" mode). Use
  `swr_set_compensation()` in the existing `SwrContext` for inaudible soft resampling (FFmpeg is already linked).
  Resync hard when the error is above ~50 ms.
- `HairTunes` (AP1) already has a sample-stuffing strategy that could be shared via `IAudioSession`.

## Password and PIN

**Today:** if a password is configured, AP2 is not advertised (AP1 keeps password protection). Transient pairing always
uses PIN `3939`.

**How to add:**
- *Password:* advertise TXT `pw=true` and set flag bit 7 (`0x80`, "password required") in `flags`/`sf`. In
  `Pairing::HandlePairSetup` with the transient flag, iOS then prompts for the password and uses it as the SRP password
  with the user name `"AirPlay"` (instead of `"Pair-Setup"`/`3939`). Map the existing `Password` config value to it.
  Return `TlvError::Authentication` (and back off after repeated failures, `TlvError::Backoff`) on a mismatch.
- *On-screen PIN:* handle `POST /pair-pin-start`: generate a 4-digit PIN, show it in the UI (a new `IRaopEvents`
  callback → message box / toast), then run the SRP with that PIN. Set flag bit 3 (`0x8`, "PIN required").
- Extend `IsEnabled()` to allow AP2 with a password once this exists.

## HomeKit pairing

**Today:** only *transient* pair-setup (flag `0x10`, M1–M4) is supported. Pair-verify works with any controller
(we sign with our Ed25519 key but don't verify the controller's long-term key). `pair-add`, `pair-remove` and
`pair-list` return 501.

**How to add:**
- Implement pair-setup M5/M6. Decrypt the M5 sub-TLV with ChaCha20-Poly1305 (`Pair-Setup-Encrypt-Salt`/`-Info`,
  nonce `PS-Msg05`) and verify the controller signature over `HKDF(K, "Pair-Setup-Controller-Sign-Salt", "…-Info") ||
  pairingID || LTPK`. Store `{pairingID → LTPK, permissions}` in the config (a new key such as `AirPlay2Controllers`). Reply
  with our own signed accessory info (M6, nonce `PS-Msg06`).
- In pair-verify M3, look up the controller's `Identifier`, verify its signature over `ourPublicKey || identifier ||
  theirPublicKey`, and reject unknown controllers when access control is set to "only paired devices".
- Implement `pair-add` / `pair-remove` / `pair-list` (TLV8 method 3/4/5, admin permission check). These are used by the
  Home app.
- Add the TXT/info fields `acl` (0 = everyone, 1 = same network, 2 = only paired) and an "Access control" option in the UI.
- Persisting `pi`/`pk` already exists, so the identity stays stable across restarts.
- Reference: HAP specification (non-commercial version), shairport-sync `pair_ap` (MIT, from owntone), pyatv.

## Realtime retransmissions

**Today:** realtime (type 96, UDP) packets are decrypted and reordered in the jitter queue. Lost packets are not requested
again. (Buffered audio uses TCP and doesn't need this.)

**How to add:** detect gaps in the 16-bit sequence numbers and send RTCP resend requests (`0x80 0xD5 seq count`) to the
sender's control port (from SETUP). Accept the answers (`0xD6`, which wraps an audio packet at offset 4) on our control
port, which `Ap2AudioSession` already opens but ignores. This works like `HairTunes`' AP1 resend logic, which can be
reused. Also handle the sync packets (`0xD4`) to update the realtime anchor when no PTP is available.

## Audio formats

**Today:** ALAC 44.1/48 kHz 16/24 bit and AAC-LC 44.1/48 kHz. Everything is converted to 44.1 kHz/16 bit stereo for output.
Advertised `supportedFormats` are limited to what's decoded.

**How to add:**
- *PCM* (`audioFormat` bits 2–11): no decoder needed. Byte-swap big-endian S16/S24 and resample through the existing
  `SwrContext`.
- *AAC-ELD* (bits 24–26, used by some macOS/realtime streams): FFmpeg's native `aac` decoder doesn't decode ELD
  reliably. Use `libfdk-aac` through FFmpeg (`libfdk_aac` decoder, check licensing for binary distribution) or build
  FFmpeg with it. Codec extradata = ELD AudioSpecificConfig (`0xF8 0xE8 0x50 0x00` for 44.1 kHz/480 spf).
- *Opus* (`0x1B000000` SSRC, 48 kHz): FFmpeg `libopus`/native `opus` decoder. Map the SSRC and advertise the
  bit in `supportedFormats`.
- *Hi-res output:* lift the 44.1 kHz/16 bit limit of the output pipe (the WAV header in the `BlobStream` already
  carries the format; ALSA/WASAPI playback must honour it) to play 48 kHz/24 bit natively without resampling.
- Advertise new formats in `GET /info` → `supportedFormats` and in `AudioFormatFromAirPlayFormat`.

## Remote control

**Today:** remote control uses DACP (AirPlay 1 style). The `DACP-ID`/`Active-Remote` headers are forwarded to
the existing DACP browser, so play/pause/next/volume from the UI, SMTC and MPRIS still work. Recent iOS versions don't
always advertise `_dacp._tcp` for AP2 sessions, though. The AP2 event channel (SETUP phase 1 `eventPort`) is accepted and
drained, but we never send anything on it.

**How to add (MediaRemote over the event channel):**
- Encrypt the event channel with `SecureChannel::ForEvents(secret)` (salt `Events-Salt`; the read/write keys are swapped
  compared to the control channel because *we* are the client on this connection).
- Send RTSP-style `POST /command` requests with a bplist body
  `{ type: "sendMediaRemoteCommand", params: { command: <MRCommand> } }` (play = 0, pause = 1, togglePlayPause = 2,
  nextTrack = 4, previousTrack = 5, …). Also send `updateInfo` / `nowPlaying` updates as needed.
- Add an abstraction in `RaopServer::SendDacpCommand` that prefers the event channel for AP2 sessions and falls back
  to DACP.
- Reference: pyatv `protocols/airplay` (MediaRemote tunnelled over AirPlay), owntone `airplay.c`.

## Bplist metadata

**Today:** feature bit 50 is *not* set, so iOS sends DMAP metadata and artwork through `SET_PARAMETER`. The AP1 code
handles that, so title, artist, album, cover and progress work.

**How to add:** set bit 50, then handle `SET_PARAMETER` / `POST /feedback` bodies with
`Content-Type: application/x-apple-binary-plist` (keys `params.kMRMediaRemoteNowPlayingInfo…`: title, artist, album,
duration, elapsed time, artwork data). Map them onto `IRaopEvents::OnSetCurrentDmapInfo/OnSetCurrentImage` and progress.
Benefit: richer metadata (e.g. duration and playback rate without polling) and fewer round-trips.

## Multi-room

**Today:** each ShairportQt instance plays on its own. With working PTP, two instances (or ShairportQt + HomePod) are
roughly in sync because both play at the anchor time, but group handling is ignored.

**How to add:**
- `SETPEERS`/`SETPEERSX`: parse the address list (and in SETPEERSX the clock IDs), restrict PTP to these peers and pass
  them to nqptp if it's used. Currently we answer 200 and ignore the body.
- `gid`/`gcgl`/`igl` TXT and `/info` fields: keep the group UUID from SETUP (`groupUUID`) and report it, so the sender
  shows the device as part of the group. Handle `isMultiSelectAirPlay` / group leader changes.
- Device latency reporting: advertise the real output latency in SETUP response streams `audioLatency`/`latencyMin` so
  that the sender can align devices with different latencies.
- Requires [PTP](#ptp-timing) and ideally [drift compensation](#drift-compensation).

## Volume and misc commands

- `SETRATE` (rate without anchor) currently returns 501. Implement it as pause/resume of the current anchor.
- `POST /audioMode` (`default`/`moviePlayback`), `POST /configure` (persistent group / device name changes) and
  `POST /feedback` currently get 200. They could change latency targets or rename the receiver.
- Hardware volume: answer `volumeControlType` according to whether the output device supports hardware volume, and
  report volume changes made locally back to the sender (event channel `updateInfo`).

## Photos

Classic AirPlay photos (`PUT /photo`, JPEG body, `X-Apple-AssetKey`, slideshow `/slideshow-features`) on the *AirPlay*
HTTP side (port 7000). AP2 senders reach it after pairing. Implementation: route `PUT /photo` in the AP2 handler,
decode with `QImage`, show a full-screen `QWidget`/`QLabel`, and set feature bits 1 and 5 (slideshow). Little crypto
work. It needs a UI (a new window) and is out of scope for an audio receiver unless asked for.

## Video URL casting

"AirPlay video" where the sender passes a URL (YouTube-less HLS, the Photos app, Safari videos):
`POST /play` (bplist: `Content-Location`, `Start-Position`), `POST /scrub`, `POST /rate`, `GET /playback-info`,
`POST /stop`, `/action` (FairPlay-protected playlists, unplayable without Apple keys), and the reverse HTTP
event connection (`POST /reverse`, `Upgrade: PTTH/1.0`).

Implementation: a video player window based on Qt Multimedia (`QMediaPlayer` + `QVideoWidget`, FFmpeg backend on Qt 6)
or `libmpv`, plus the request handlers and periodic `playback-info` plists. Set feature bits 0 (video), 4 (HLS)
and 3 (volume). Many apps (Netflix, Disney+…) use FairPlay-protected streams and won't work. YouTube in Safari and
personal videos will. Reference: UxPlay (`lib/http_handlers.h`, GPL-3), RPiPlay.

## Screen mirroring

Requires the FairPlay SAP handshake (`POST /fp-setup`, 2 messages) to decrypt the AES key of the mirroring stream.
Open-source receivers (UxPlay, RPiPlay) use the reverse-engineered `playfair` code (GPL-3, unclear legal status) for
this. Then: the `SETUP` mirroring stream (type 110) with an `ekey`/`eiv`, AES-CTR decryption of the H.264/H.265 NAL
stream on a dedicated TCP port, decoding (FFmpeg/hardware decoders), rendering in a Qt window with correct timing,
AAC-ELD audio for the mirroring audio stream (type 96 with `ct=8`), and the NTP or PTP time sync. Set feature bits 7
(mirroring) and 8 (rotate), plus `/info` displays.

Effort is dominated by the video pipeline and by the FairPlay question. Check the legal side first. Reference: UxPlay.

## Windows

- vcpkg: add `ffmpeg[avcodec,swresample]` (static triplet `x64-windows-static` / `arm64-windows-static`) and check that
  `find_package(FFMPEG)` sets `FFMPEG_INCLUDE_DIRS`/`FFMPEG_LIBRARIES` (vcpkg's `FindFFMPEG.cmake` wrapper does). The
  Windows build of this branch hasn't been tested yet.
- `DnsSD::RegisterService` uses the native `DnsServiceRegister` path for `_airplay._tcp` as well. Verify that the
  TXT records with 20 entries are accepted (length limits) by both Bonjour and native DNS-SD.
- Firewall rule: the installer / first-run prompt should allow the dynamic TCP/UDP ports plus UDP 319/320 (PTP).
- PTP on Windows: binding 319/320 doesn't need admin rights unless the Windows Time service with PTP provider is active.

## Packaging and testing

- Debian: runtime deps are in `debian/control`. A `postinst` could run `setcap cap_net_bind_service=+ep` for PTP.
- Flatpak/AppImage: bundle FFmpeg or use the runtime's FFmpeg extension.
- Interop matrix to test before removing the "experimental" label: iOS 16/17/18 (Music, Spotify, YouTube, Podcasts),
  macOS (Music, system audio output), Apple TV/HomePod groups, phone call interruption (the original issue #33),
  Siri announcements, switching back and forth between AirPlay 1 and 2 senders, and long sessions (drift).
- Automated: extend the manual harness `AirPlay2.DISABLED_ServeForManualTesting` with the scripted client
  `test/tools/ap2_client.py` and run it in CI with a fake ALSA device
  (`snd-dummy` / `pcm.null`).
