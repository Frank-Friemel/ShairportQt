# Investigation: AirPlay 2 support (issue #33), while keeping AirPlay 1

## 1. What the issue is actually asking for

Issue #33 ("AirPlay 2") comes from Shairport4w#24: **when the iPhone gets a phone call, it disconnects from Shairport.**

That happens because of how iOS handles each protocol:

- **AirPlay 1 (RAOP)** takes over the phone's *system audio route*. A phone call needs that route, so iOS
  tears down the RAOP session.
- **AirPlay 2** lets apps stream with `AVAudioSession.RouteSharingPolicy.longFormAudio`. The audio is
  *buffered* on the receiver, and the session isn't tied to the phone's local audio route. So a call can
  ring and be answered on the phone while music keeps playing on the speaker.

Apple only applies long-form route sharing to AirPlay 2 receivers. **No change to the AirPlay 1 code
can fix this.** The receiver has to implement AirPlay 2's *buffered audio* mode (stream type 103), and
that also needs PTP timing.

## 2. Current state of the codebase (relevant parts)

| Area | File(s) | What it does today | AirPlay 2 gap |
|---|---|---|---|
| mDNS advertising | `lib/dnssd.cpp` | Advertises `_raop._tcp` only (`et=0,1`, `cn=0,1`, `tp=UDP`, `vs=<app version>`) | Must also advertise `_airplay._tcp` with `features`, `flags`, `deviceid`, `pk`, `pi`, `gid`, `model`, `srcvers` ≥ 366, and `_raop._tcp` must carry matching features bits |
| RTSP server | `lib/RaopServer.cpp` (single `Get(".+")` handler), `inc/httplib/httplib_raop.h` (patched cpp-httplib) | Plain-text RTSP: OPTIONS, ANNOUNCE (SDP), SETUP (UDP ports in `Transport:` header), RECORD, FLUSH, TEARDOWN, GET/SET_PARAMETER, `Apple-Challenge` | AirPlay 2 uses `GET /info`, `POST /pair-setup`, `POST /pair-verify`, `POST /fp-setup` (optional), `POST /feedback`, `POST /command`, `SETPEERS`, `SETRATEANCHORTIME`, `FLUSHBUFFERED`, two-phase SETUP with **binary plist** bodies, and **after pair-verify, the whole TCP connection is encrypted** (ChaCha20-Poly1305 framed blocks) |
| Crypto | `lib/crypto.cpp` (RSA via BCrypt on Windows / OpenSSL on Linux; AES-CBC) | RSA-OAEP key unwrap, RSA sign for Apple-Challenge, AES-128-CBC packet decrypt | Needs SRP-6a (3072-bit, SHA-512), Ed25519, X25519, HKDF-SHA512, ChaCha20-Poly1305 (IETF, 12-byte nonce, and the 8-byte-nonce variant for audio). OpenSSL 1.1.1+/3.x covers all of it except SRP (needs a small custom implementation or csrp) |
| Audio transport | `lib/HairTunes.cpp`, `lib/RaopEndpoint.cpp` | UDP data/control/timing endpoints, resend requests, jitter queue, AES-CBC decrypt, built-in ALAC decoder | Keep for AP1 and for AP2 *realtime* (type 96). Buffered (type 103) needs a **TCP** data stream, ChaCha20-Poly1305 per-packet decrypt, a large (~8 MB / ~45 s) buffer, and playback started by anchor time |
| Codecs | ALAC decoder embedded in `HairTunes.cpp` | ALAC/PCM 44.1 kHz 16-bit | Buffered stream is usually **AAC-LC** (sometimes ALAC). Needs an AAC decoder (FFmpeg/libavcodec or fdk-aac; Windows Media Foundation is another option) |
| Clock sync | None really (timing endpoint exists, playback is driven by the queue) | Buffer-driven playback | AirPlay 2 needs **PTP (IEEE 1588 / gPTP-style)** on UDP 319/320, clock follow, and anchor-time → local-clock mapping. Also needs **drift correction** (stuffing or resampling) to stay in sync with the sender and any other AP2 speakers |
| Persistent identity | `HWaddress` in config | MAC-based name | Needs a persistent Ed25519 long-term key pair (`pk`), a stable `pi` UUID, and storage for paired-controller keys (if HomeKit-style pairing is supported) |
| Metadata / remote | `lib/DmapParser.cpp`, `app/DacpService.cpp` | DMAP metadata via SET_PARAMETER, DACP remote control | AP2 still sends DMAP/artwork via SET_PARAMETER over the encrypted channel (reusable). Remote control can still use DACP (`Active-Remote`, `DACP-ID`); the newer MediaRemote-over-event-channel path is optional |

Tests (`test/`) cover endpoints, networking, queues, streams and trimming. There are no protocol-level
tests. CI (`.github/workflows/build.yaml`) builds on Ubuntu with apt packages plus sockpp. Windows
builds use vcpkg (static).

## 3. Work breakdown

Estimates assume one developer who knows C++ and this codebase, using shairport-sync, the
openairplay/airplay2-receiver Python prototype, and the emanuelecozzi/openairplay protocol notes as
references. They're rough and include debugging against real iOS/macOS senders, which is where most of
the time goes.

### Phase 0: Preparation (≈ 1 week)
- Refactor `RaopServer.cpp`. Today it's one big lambda with an `if/else` per method. Split it into a
  per-connection session object with a method dispatch table, so AP1 and AP2 handlers can live side by
  side.
- Add a binary plist reader/writer. Options: libplist (LGPL-2.1, works with vcpkg/apt) or a small
  custom bplist00 encoder/decoder (~500 LOC, since only dict/array/string/int/data/bool/real are needed).
- Add the new deps: libplist (optional), an AAC decoder (FFmpeg or fdk-aac), and possibly libsodium as
  an alternative to OpenSSL for Ed25519/X25519/ChaCha. Add them to CMake, vcpkg, CI apt, and the
  Debian packaging (`debian/`).

### Phase 1: Discovery and `/info` (≈ 3–5 days)
- Advertise `_airplay._tcp` next to `_raop._tcp` on the same port (7000 is usual, but any port
  works). Do it in all three backends in `lib/dnssd.cpp`: native Windows DNS-SD, Bonjour `dns_sd`, and
  the Linux path.
- Build the `features` bitmask. Start with shairport-sync's AP2 set (e.g. `0x1C340405D4A00` with
  buffered audio, PTP, and unified pair-setup), and make sure the legacy bits stay set so AP1 senders
  (old iTunes, Android AirPlay apps, other shairport clients) still fall back to RAOP.
- `GET /info` returns a bplist with features, name, deviceID, pk, model, sourceVersion, and so on.

### Phase 2: Pairing and encrypted control channel (≈ 2–3 weeks), the main protocol hurdle
- **Transient pairing** (no PIN, `X-Apple-HKP: 4`). This is the minimum iOS needs for "open"
  receivers:
  - `pair-setup` M1–M4: SRP-6a with user `Pair-Setup`, PIN `3939`, TLV8 encoding.
  - `pair-verify` M1–M4: X25519 key exchange, then Ed25519 signatures, then ChaCha20-Poly1305
    encrypted TLV.
- **Encrypted RTSP framing**. Once pair-verify succeeds, every read and write on that socket is
  `[2-byte LE length][ciphertext][16-byte tag]` with `Control-Read/Write-Encryption-Key` derived via HKDF.
  - This is the biggest structural change. `httplib_raop.h` reads directly from `SocketStream`. You
    need a `Stream` decorator (e.g. `EncryptedSocketStream`) that can be swapped in **mid-connection**,
    and the patched httplib's `process_request` loop has to allow that.
- Optional: **full HomeKit pairing** (PIN pairing, persisted controller keys, `/pair-add`,
  `/pair-remove`, `/pair-list`). That needs a "pairing PIN" UI and storage in `MainDlg`.
  It's only needed to show up in the Home app, not to fix #33. **+1–2 weeks** if wanted.
- **FairPlay (`/fp-setup`)**: shairport-sync shows AP2 audio works **without** FairPlay as long as the
  advertised features don't require it. Don't implement it. The only open source implementation
  (playfair) carries legal and licensing risk.
- Keep the password (Digest auth) feature working for AP1. For AP2, a password means switching to the
  password-based pair-setup path (`X-Apple-HKP: 3`), with the password as the SRP secret. **+3–5 days.**

### Phase 3: AirPlay 2 session setup (≈ 1–1.5 weeks)
- First `SETUP` (bplist): session info, `timingProtocol: PTP`, `timingPeerInfo`. Reply with
  `eventPort` and `timingPort`.
- **Event channel**: a TCP listener that iOS connects to. It can stay mostly idle, but it must also be
  encrypted with the `Events-*` HKDF keys.
- Second `SETUP` (bplist `streams[]`): stream `type` 96 (realtime) or 103 (buffered), `ct` (compression
  type: ALAC/AAC/OPUS), `shk` (shared ChaCha key), `spf`, `audioFormat`. Reply with `dataPort`,
  `controlPort`, and `audioBufferSize`.
- `SETPEERS`/`SETPEERSX`, `SETRATEANCHORTIME`, `FLUSHBUFFERED`, `GET_PARAMETER`/`SET_PARAMETER`
  (volume, progress, DMAP and artwork, which are reusable), `TEARDOWN` with a body (stream vs. whole
  session), and `POST /feedback` (keep-alive, must return 200).

### Phase 4: Audio pipeline (≈ 2–3 weeks)
- **Realtime (type 96)**: very close to the current AP1 path. Reuse `HairTunes` and swap AES-CBC for
  ChaCha20-Poly1305 (nonce = last 8 bytes of the packet, AAD = RTP header bytes 4–11). It's used by
  some senders for "low-latency" or system audio.
- **Buffered (type 103)**, which is **what fixes #33**:
  - TCP data socket. Packets are `[2-byte BE length][RTP header][ChaCha20-Poly1305 payload][tag][nonce]`.
  - Decode AAC-LC (or ALAC) into PCM, then put it in a large timestamped buffer.
  - Playback controlled by `SETRATEANCHORTIME`: map (RTP time, network PTP time) to local time, then
    start, pause, or seek. `FLUSHBUFFERED` drops ranges of the buffer.
  - Integrate with `AudioPlayer`/`AlsaAudio`/WASAPI (`lib/audio/`). Those players need a "play this
    frame at local time T" mode instead of "play as soon as queued".
- **Drift correction**: add/drop frames (simple) or resample (soxr/libsamplerate). Even a
  single-speaker setup slowly drifts away from the sender's PTP clock without it.

### Phase 5: PTP timing (≈ 2–4 weeks), the main systems-level hurdle
- AP2 senders act as PTP masters on UDP **319 (event)** and **320 (general)**. The receiver has to
  listen to Sync/Follow_Up/Announce, track the master clock offset, and send Signaling where needed.
- Port problems:
  - **Linux**: 319/320 are privileged ports. shairport-sync solves this with a separate daemon
    (**nqptp**) that has `CAP_NET_BIND_SERVICE` and talks to the player over shared memory. For
    ShairportQt (a desktop app, not run as root) the choices are:
    (a) ship a small setcap helper or a systemd service, (b) `setcap` the ShairportQt binary at
    install time (`linux/install.sh`, `debian/postinst`), or (c) reuse nqptp itself when it's
    installed. Option (b) is simplest but needs a packaging change.
  - **Windows**: no privileged-port restriction, but Windows Time or other PTP software may already
    hold the ports. Needs a firewall rule in the installer (`windows/ShairportQt.iss`).
  - Only one process per host can own the ports. Running alongside shairport-sync/nqptp, or several
    ShairportQt instances, needs a shared-clock design.
- Also: support for multiple timing peers (multi-room, `SETPEERS`), and handling clock master
  changes mid-stream.

### Phase 6: Coexistence with AirPlay 1, UI, packaging (≈ 1 week)
- **AP1 is kept as-is**: same `_raop._tcp` service, same RSA/Apple-Challenge path, same `HairTunes`
  UDP/AES-CBC pipeline. The dispatcher sends a connection to the AP1 or AP2 handler based on the first
  request (ANNOUNCE/SDP means AP1; `/info`, `/pair-*`, or a bplist `SETUP` means AP2). Senders choose
  the protocol from the advertised `features`. AP1-only senders ignore AP2 bits, and iOS falls back to
  RAOP if AP2 pairing fails.
- Add a settings toggle, "Enable AirPlay 2 (experimental)", which defaults to off at first. It
  controls whether `_airplay._tcp` and the AP2 feature bits are advertised, so the AirPlay 1 behavior
  stays exactly as it is today when it's off.
- Localization strings for the new UI (`app/localization/*.cpp`: English, German, Spanish, Catalan,
  Japanese).
- Update the CI workflow, vcpkg manifest/presets, Debian control/postinst, the Windows installer
  (firewall rules for 319/320 + event/data ports), and the Readme.

### Phase 7: Testing and hardening (≈ 2 weeks)
- Unit tests in `test/`: TLV8, bplist round-trip, SRP and pair-verify against known vectors (shairport-sync and
  pyatv have them), ChaCha framing, PTP message parsing, anchor-time math.
- Interop matrix: iOS 17/18/26, macOS Music, Apple TV as sender, Android AirPlay apps (AP1), and
  multi-room with an HomePod/AirPort Express or shairport-sync.
- **Regression check for AP1** on every supported sender.
- Specific test for #33: start music on iPhone, receive a call, confirm playback continues (buffered
  mode only).

## 4. Summary

| Phase | Effort |
|---|---|
| 0. Refactor + deps (bplist, AAC, crypto) | ~1 week |
| 1. mDNS `_airplay._tcp` + `/info` | 3–5 days |
| 2. Transient pairing + encrypted RTSP | 2–3 weeks |
| 2b. (optional) HomeKit PIN pairing / AP2 password | +1.5–3 weeks |
| 3. AP2 SETUP / event channel / commands | 1–1.5 weeks |
| 4. Realtime + **buffered** audio, AAC, timed playback, drift | 2–3 weeks |
| 5. PTP clock (incl. Linux privileged-port solution) | 2–4 weeks |
| 6. AP1/AP2 coexistence, UI, packaging | ~1 week |
| 7. Tests + interop | ~2 weeks |
| **Total (minimum to fix #33)** | **≈ 11–16 person-weeks (~3–4 months)** |
| **Total incl. optional pairing/password** | **≈ 13–19 person-weeks** |

Estimated new or changed code: about **6–10 kLOC** of C++. That breaks down roughly as: pairing/crypto
1.5–2k, bplist/TLV 0.7k, AP2 RTSP/session 1.5–2k, buffered player + timing 2–3k, PTP 1–2k, plus
tests.

## 5. Risks and open questions

1. **Undocumented, moving protocol.** Apple has never published AirPlay 2, and iOS updates have broken
   third-party receivers before (feature-bit and pairing changes). Expect ongoing maintenance.
2. **Linux PTP ports 319/320** need elevated capability or a helper daemon. This is a packaging and
   UX decision the maintainer needs to make.
3. **Licensing.** ShairportQt is GPL-3.0. shairport-sync's code is mostly MIT-style. Check nqptp's
   license (GPL-2.0) for GPL-3 compatibility ("or later" clause) before copying code. FFmpeg (LGPL)
   works, fdk-aac does **not** (GPL-incompatible), so use FFmpeg/libavcodec or Media Foundation on
   Windows. Don't use playfair (FairPlay).
4. **AAC decoding on Windows static/vcpkg builds** grows the binary noticeably (FFmpeg minimal build
   with only the AAC decoder, about 1–2 MB).
5. **Audio backend timing.** The current players are queue-driven. Accurate (≤ ~1 ms) scheduled
   playback on WASAPI and ALSA needs device-latency reporting. That's the hardest part to get right
   for multi-room sync, but much less critical for the single-speaker phone-call case in #33.
6. **Alternative with much less effort:** make ShairportQt a GUI front-end that spawns or controls
   `shairport-sync` + `nqptp` on Linux. That gives AP2 in about 2–3 weeks, but not on Windows
   (shairport-sync doesn't support Windows), and it changes the project's architecture.

## 6. Recommended plan

Do phases 0–1–2–3 first with only **realtime (type 96)** streaming. That's the smallest milestone that
proves pairing and encrypted RTSP work, and AP1 stays untouched behind the toggle. Then add buffered
audio, PTP and AAC (phases 4–5). That second step is what actually fixes the phone-call disconnect in
#33.
