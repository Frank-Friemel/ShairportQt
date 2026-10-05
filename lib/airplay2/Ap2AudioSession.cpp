#include "airplay2/Ap2AudioSession.h"
#include "airplay2/PtpClock.h"
#include "audio/PlaySound.h"
#include "audio/WaveHeader.h"
#include "SuspendInhibitor.h"
#include "sockpp/tcp_acceptor.h"
#include "sockpp/inet_address.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <cstring>

using namespace std;
using namespace std::chrono_literals;

namespace AirPlay2
{
    namespace
    {
        constexpr size_t MaxQueuedFrames = 8192;
        constexpr size_t MaxHistoryFrames = 1024;
        constexpr int OutputBytesPerFrame = Ap2Decoder::OutputChannels * 2;
        constexpr int64_t NsPerSec = 1'000'000'000LL;

        uint32_t ReadBE32(const uint8_t* p) noexcept
        {
            return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
        }

        // true if timestamp a is before b (RTP timestamps wrap around)
        bool TsBefore(uint32_t a, uint32_t b) noexcept
        {
            return static_cast<int32_t>(a - b) < 0;
        }

        bool WaitReadable(sockpp::socket_t handle, int ms) noexcept
        {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(handle, &set);
            timeval tv{ ms / 1000, (ms % 1000) * 1000 };
            return ::select(static_cast<int>(handle) + 1, &set, nullptr, nullptr, &tv) > 0;
        }

        int16_t ApplyGain(int16_t in, double gain, double& err) noexcept
        {
            const double v = in * gain + err;
            double r = std::round(v);
            err = v - r;

            if (r > 32767.)
            {
                r = 32767.;
            }
            else if (r < -32768.)
            {
                r = -32768.;
            }
            return static_cast<int16_t>(r);
        }
    }

    bool DecryptAudioPacket(const uint8_t* packet, size_t len, const Bytes& key, AudioFrame& frame)
    {
        // header (12) + tag (16) + nonce (8)
        if (len < 12 + 16 + 8 || key.size() != 32)
        {
            return false;
        }
        frame.seq = ReadBE32(packet) & 0x7fffff;
        frame.timestamp = ReadBE32(packet + 4);
        frame.ssrc = ReadBE32(packet + 8);

        const size_t cipherLen = len - 12 - 16 - 8;
        uint8_t nonce[12] = {};
        memcpy(nonce + 4, packet + len - 8, 8);

        frame.payload.resize(cipherLen);

        return Crypto::ChaChaDecrypt(key.data(), nonce, packet + 4, 8,
            packet + 12, cipherLen, packet + 12 + cipherLen, frame.payload.data());
    }

    Ap2AudioSession::Ap2AudioSession(SharedPtr<IValueCollection> config, Ap2SessionParams params, shared_ptr<PtpClock> clock)
        : m_config(move(config))
        , m_params(move(params))
        , m_clock(move(clock))
    {
        if (m_params.sharedKey.size() != 32)
        {
            throw runtime_error("invalid stream key");
        }
        if (!Ap2Decoder::IsAvailable())
        {
            throw runtime_error("no audio decoder available");
        }
        m_controlEndpoint = make_unique<RtpEndpoint>(this, m_params.clientID);

        if (m_params.type == StreamType::Realtime)
        {
            m_realtimeEndpoint = make_unique<RtpEndpoint>(this, m_params.clientID);
            m_dataPort = m_realtimeEndpoint->GetPort();

            // realtime audio doesn't wait for an anchor
            m_rate = 1.;
        }
        else
        {
            m_acceptor = make_unique<sockpp::tcp_acceptor>();

            if (!m_acceptor->open(sockpp::inet_address(static_cast<in_port_t>(0))))
            {
                throw runtime_error("could not open the buffered audio port");
            }
            m_dataPort = sockpp::inet_address(m_acceptor->address()).port();
            m_receiverThread = thread([this]() { RunBufferedReceiver(); });
        }
        m_playerThread = thread([this]()
            {
                const SuspendInhibitor suspendInhibitor{ "ShairportQt", "Playing" };
                RunPlayer();
            });
        spdlog::info("AirPlay2: {} audio session for {} on data port {}",
            m_params.type == StreamType::Realtime ? "realtime" : "buffered", m_params.clientID, m_dataPort);
    }

    Ap2AudioSession::~Ap2AudioSession()
    {
        Stop();
    }

    void Ap2AudioSession::Stop() noexcept
    {
        if (m_stop.exchange(true))
        {
            return;
        }
        {
            const lock_guard<mutex> guard(m_mtx);
            m_cond.notify_all();
            m_condSpace.notify_all();
        }
        m_realtimeEndpoint.reset();
        m_controlEndpoint.reset();

        try
        {
            if (m_receiverThread.joinable())
            {
                m_receiverThread.join();
            }
            if (m_playerThread.joinable())
            {
                m_playerThread.join();
            }
        }
        catch (...)
        {
        }
        if (m_acceptor)
        {
            m_acceptor->close();
        }
    }

    uint16_t Ap2AudioSession::GetControlPort() const noexcept
    {
        return m_controlEndpoint ? m_controlEndpoint->GetPort() : 0;
    }

    void Ap2AudioSession::OnRequest(RtpEndpoint* endpoint, unique_ptr<RtpPacket>&& packet)
    {
        if (endpoint == m_realtimeEndpoint.get())
        {
            // realtime packets: RTP header (version, payload type, 16-bit sequence number)
            OnPacket(packet->data(), packet->size());
        }
        // control packets (sync / retransmissions) are ignored
        PutPacketToPool(move(packet));
    }

    void Ap2AudioSession::OnPacket(const uint8_t* data, size_t len)
    {
        AudioFrame frame;

        if (!DecryptAudioPacket(data, len, m_params.sharedKey, frame))
        {
            spdlog::debug("AirPlay2: failed to decrypt audio packet ({} bytes)", len);
            return;
        }
        if (m_params.type == StreamType::Realtime)
        {
            // realtime packets carry a random SSRC, the format is defined by SETUP
            frame.ssrc = 0xffffffff;

            // realtime packets: 2 byte sequence number
            frame.seq &= 0xffff;
        }
        EnqueueFrame(move(frame));
    }

    void Ap2AudioSession::EnqueueFrame(AudioFrame&& frame)
    {
        unique_lock<mutex> lock(m_mtx);

        if (m_params.type == StreamType::Buffered)
        {
            // backpressure: let TCP throttle the sender
            m_condSpace.wait(lock, [this]() { return m_stop || m_queue.size() < MaxQueuedFrames; });
        }
        else if (m_queue.size() >= MaxQueuedFrames)
        {
            m_queue.pop_front();
        }
        if (m_stop || IsFlushed(frame.timestamp))
        {
            return;
        }
        if (m_params.type == StreamType::Realtime && !m_queue.empty() && TsBefore(frame.timestamp, m_queue.back().timestamp))
        {
            // out of order UDP packet
            auto i = m_queue.end();

            while (i != m_queue.begin() && TsBefore(frame.timestamp, (i - 1)->timestamp))
            {
                --i;
            }
            m_queue.insert(i, move(frame));
        }
        else
        {
            m_queue.emplace_back(move(frame));
        }
        m_cond.notify_all();
    }

    bool Ap2AudioSession::IsFlushed(uint32_t ts) const noexcept
    {
        if (!m_flushUntil.has_value())
        {
            return false;
        }
        if (m_flushFrom.has_value() && TsBefore(ts, m_flushFrom.value()))
        {
            return false;
        }
        return TsBefore(ts, m_flushUntil.value());
    }

    void Ap2AudioSession::RunBufferedReceiver() noexcept
    {
        while (!m_stop)
        {
            try
            {
                if (!WaitReadable(m_acceptor->handle(), 200))
                {
                    continue;
                }
                sockpp::tcp_socket socket = m_acceptor->accept();

                if (!socket)
                {
                    continue;
                }
                spdlog::debug("AirPlay2: buffered audio connection established");
                ReadBufferedConnection(socket);
                spdlog::debug("AirPlay2: buffered audio connection closed");
            }
            catch (const exception& e)
            {
                spdlog::error("AirPlay2: buffered audio receiver: {}", e.what());
            }
        }
    }

    void Ap2AudioSession::ReadBufferedConnection(sockpp::tcp_socket& socket) noexcept
    {
        vector<uint8_t> buffer;
        buffer.reserve(64 * 1024);
        uint8_t chunk[16 * 1024];

        while (!m_stop)
        {
            if (!WaitReadable(socket.handle(), 200))
            {
                continue;
            }
            const auto n = socket.read(chunk, sizeof(chunk));

            if (n <= 0)
            {
                return;
            }
            buffer.insert(buffer.end(), chunk, chunk + n);

            size_t pos = 0;

            // [uint16 BE length including these 2 bytes][packet]
            while (buffer.size() - pos >= 2)
            {
                const size_t packetLen = (size_t(buffer[pos]) << 8) | buffer[pos + 1];

                if (packetLen < 2)
                {
                    spdlog::error("AirPlay2: invalid buffered audio packet length");
                    return;
                }
                if (buffer.size() - pos < packetLen)
                {
                    break;
                }
                OnPacket(buffer.data() + pos + 2, packetLen - 2);
                pos += packetLen;

                if (m_stop)
                {
                    return;
                }
            }
            buffer.erase(buffer.begin(), buffer.begin() + pos);
        }
    }

    void Ap2AudioSession::SetRateAnchor(double rate, optional<uint32_t> rtpTime, optional<int64_t> networkTimeNs, uint64_t clockId)
    {
        const lock_guard<mutex> guard(m_mtx);

        if (rtpTime.has_value())
        {
            m_anchorValid = true;
            m_anchorRtp = rtpTime.value();
            m_anchorLocalNs.reset();

            int64_t localNs = 0;

            if (networkTimeNs.has_value() && m_clock && m_clock->NetworkToLocal(clockId, networkTimeNs.value(), localNs))
            {
                m_anchorLocalNs = localNs;
            }
        }
        const bool wasPlaying = m_rate > 0.;
        m_rate = rate;

        if (wasPlaying && rate > 0. && rtpTime.has_value())
        {
            // new anchor while playing (e.g. seeking): resynchronize
            m_restartPending = true;
        }
        spdlog::debug("AirPlay2: rate {} anchor rtp {} ({})", rate, m_anchorRtp, m_anchorLocalNs.has_value() ? "ptp" : "local timing");
        m_cond.notify_all();
    }

    void Ap2AudioSession::FlushBuffered(optional<uint32_t> fromTimestamp, uint32_t untilTimestamp)
    {
        const lock_guard<mutex> guard(m_mtx);

        m_flushFrom = fromTimestamp;
        m_flushUntil = untilTimestamp;

        const auto isFlushed = [this](const AudioFrame& f) { return IsFlushed(f.timestamp); };

        m_queue.erase(remove_if(m_queue.begin(), m_queue.end(), isFlushed), m_queue.end());
        m_history.erase(remove_if(m_history.begin(), m_history.end(), isFlushed), m_history.end());

        if (!fromTimestamp.has_value())
        {
            // immediate flush: stop the output, wait for a new anchor
            m_restartPending = true;
            m_anchorValid = false;
        }
        m_condSpace.notify_all();
        m_cond.notify_all();
    }

    void Ap2AudioSession::Flush(unsigned int) noexcept
    {
        try
        {
            const lock_guard<mutex> guard(m_mtx);
            m_queue.clear();
            m_history.clear();
            m_restartPending = true;
            m_condSpace.notify_all();
            m_cond.notify_all();
        }
        catch (...)
        {
        }
    }

    bool Ap2AudioSession::ShouldPlay() const noexcept
    {
        if (m_params.type == StreamType::Realtime)
        {
            return true;
        }
        return m_rate > 0. && m_anchorValid;
    }

    void Ap2AudioSession::ResetProgess() noexcept
    {
        m_progressData = 0;
    }

    int Ap2AudioSession::GetProgressTime() const noexcept
    {
        const int64_t d = m_progressData.load() - m_pendingData.load();

        if (d <= 0)
        {
            return 0;
        }
        return static_cast<int>(d / (Ap2Decoder::OutputSampleRate * OutputBytesPerFrame));
    }

    bool Ap2AudioSession::IsPlaying() const noexcept
    {
        return m_outputActive;
    }

    uint64_t Ap2AudioSession::GetSamplingFreq() const noexcept
    {
        return static_cast<uint64_t>(m_params.format.sampleRate);
    }

    bool Ap2AudioSession::StartOutput()
    {
        m_pcm = MakeShared<BlobStream>();
        m_pcm->SetMode(BlobStream::Mode::pipeOpen);

        AlsaAudio::WaveHeader hdrWav;
        hdrWav.init(Ap2Decoder::OutputSampleRate, 16, Ap2Decoder::OutputChannels);
        m_pcm->Write(&hdrWav, hdrWav.mySize(), nullptr);

        m_errLeft = m_errRight = 0.;
        return true;
    }

    void Ap2AudioSession::StopOutput() noexcept
    {
        try
        {
            if (m_pcm)
            {
                // drop what's not been played yet and end the stream
                m_pcm->Clear();
                m_pcm->SetMode(BlobStream::Mode::pipeClosed);

                if (m_playAudio.valid())
                {
                    m_playAudio.wait_for(2s);
                }
            }
        }
        catch (...)
        {
        }
        m_playAudio = {};
        m_pcm.Clear();
        m_outputActive = false;
        m_pendingData = 0;

        if (m_decoder)
        {
            m_decoder->Reset();
            m_skipNextBlock = m_decoder->Format().codec == AudioCodec::AAC;
        }
    }

    void Ap2AudioSession::WriteSilence(int64_t durationNs)
    {
        const int64_t frames = durationNs * Ap2Decoder::OutputSampleRate / NsPerSec;

        if (frames <= 0 || !m_pcm)
        {
            return;
        }
        const vector<uint8_t> silence(static_cast<size_t>(frames) * OutputBytesPerFrame, 0);
        m_pcm->Write(silence.data(), static_cast<ULONG>(silence.size()), nullptr);
    }

    void Ap2AudioSession::WriteFrame(const AudioFrame& frame)
    {
        AudioFormat format = m_params.format;

        if (frame.ssrc == 0)
        {
            // SSRC 0: no audio
            return;
        }
        if (frame.ssrc != 0xffffffff && !AudioFormatFromSsrc(frame.ssrc, format))
        {
            // unknown format id: assume the format announced by SETUP
            if (!m_unknownSsrcLogged)
            {
                m_unknownSsrcLogged = true;
                spdlog::warn("AirPlay2: unknown audio SSRC 0x{:08x}, using the SETUP format", frame.ssrc);
            }
            format = m_params.format;
        }
        if (!m_decoder || memcmp(&m_decoder->Format(), &format, sizeof(format)) != 0)
        {
            m_decoder = Ap2Decoder::Create(format);
            m_skipNextBlock = format.codec == AudioCodec::AAC;

            if (!m_decoder)
            {
                return;
            }
        }
        vector<int16_t> pcm;

        if (!m_decoder->Decode(frame.payload.data(), frame.payload.size(), pcm) || pcm.empty())
        {
            spdlog::debug("AirPlay2: failed to decode audio frame {} ({} bytes)", frame.seq, frame.payload.size());
            return;
        }
        if (!m_firstFrameDecoded)
        {
            m_firstFrameDecoded = true;
            spdlog::info("AirPlay2: first audio frame decoded ({} samples)", pcm.size() / Ap2Decoder::OutputChannels);
        }
        if (m_skipNextBlock)
        {
            // the first decoded AAC block contains a glitch
            m_skipNextBlock = false;
            fill(pcm.begin(), pcm.end(), int16_t(0));
        }
        const int64_t volumeDb = VariantValue::Key("Volume").TryGet<int64_t>(m_config).value_or(0);

        if (volumeDb != 0)
        {
            const double gain = volumeDb <= -144000 ? 0. : pow(10.0, volumeDb * 0.00005);

            for (size_t i = 0; i + 1 < pcm.size(); i += 2)
            {
                pcm[i] = ApplyGain(pcm[i], gain, m_errLeft);
                pcm[i + 1] = ApplyGain(pcm[i + 1], gain, m_errRight);
            }
        }
        ULONG written = static_cast<ULONG>(pcm.size() * sizeof(int16_t));
        m_pcm->Write(pcm.data(), written, &written);
        m_progressData += written;
    }

    void Ap2AudioSession::RunPlayer() noexcept
    {
        const int64_t msStartFill = static_cast<int64_t>(VariantValue::Key("StartFill").TryGet<size_t>(m_config).value_or(500));
        const auto audioDevice = VariantValue::Key("AudioDevice").TryGet<string>(m_config).value_or("default"s);
        const int64_t startFillBytes = max<int64_t>(msStartFill, 100) * Ap2Decoder::OutputSampleRate / 1000 * OutputBytesPerFrame;

        // amount of decoded audio which we keep in the output pipe (buffered audio)
        const int64_t targetBytes = startFillBytes + Ap2Decoder::OutputSampleRate / 2 * OutputBytesPerFrame;

        unique_lock<mutex> lock(m_mtx);

        while (!m_stop)
        {
            try
            {
                if (m_restartPending || (!ShouldPlay() && m_pcm))
                {
                    const bool flushed = m_restartPending;
                    m_restartPending = false;

                    if (m_pcm)
                    {
                        lock.unlock();
                        StopOutput();
                        lock.lock();

                        // frames which went to the output (but maybe weren't played) are re-queued
                        // so a pause / resume doesn't lose audio - they'll be filtered by the next anchor
                        m_queue.insert(m_queue.begin(), make_move_iterator(m_history.begin()), make_move_iterator(m_history.end()));
                        m_history.clear();
                    }
                    if (flushed)
                    {
                        continue;
                    }
                }
                if (!ShouldPlay() || m_queue.empty())
                {
                    m_cond.wait_for(lock, 100ms);
                    continue;
                }

                if (!m_pcm)
                {
                    const int samplingRate = m_params.format.sampleRate;

                    if (m_params.type == StreamType::Buffered)
                    {
                        // skip everything before the anchor
                        while (!m_queue.empty() && TsBefore(m_queue.front().timestamp, m_anchorRtp))
                        {
                            m_queue.pop_front();
                        }
                        if (m_queue.empty())
                        {
                            m_condSpace.notify_all();
                            continue;
                        }
                    }
                    else
                    {
                        // realtime: wait for the start fill
                        const int64_t queuedNs = static_cast<int64_t>(m_queue.size()) * m_params.format.framesPerPacket * NsPerSec / samplingRate;

                        if (queuedNs < msStartFill * 1'000'000)
                        {
                            m_cond.wait_for(lock, 20ms);
                            continue;
                        }
                    }
                    int64_t silenceNs = 0;

                    if (m_params.type == StreamType::Buffered && m_anchorLocalNs.has_value())
                    {
                        // play the first frame at the time requested by the anchor
                        const int64_t now = LocalTimeNs();
                        int64_t playAt = m_anchorLocalNs.value() + static_cast<int64_t>(m_queue.front().timestamp - m_anchorRtp) * NsPerSec / samplingRate;

                        // drop frames which are already late
                        while (playAt < now && m_queue.size() > 1)
                        {
                            m_queue.pop_front();
                            playAt = m_anchorLocalNs.value() + static_cast<int64_t>(m_queue.front().timestamp - m_anchorRtp) * NsPerSec / samplingRate;
                        }
                        silenceNs = max<int64_t>(playAt - now, 0);

                        // more than a few seconds ahead is suspicious (clock problems)
                        if (silenceNs > 5 * NsPerSec)
                        {
                            spdlog::warn("AirPlay2: anchor is {} ms ahead, ignoring it", silenceNs / 1'000'000);
                            silenceNs = 0;
                        }
                    }
                    lock.unlock();
                    StartOutput();
                    WriteSilence(silenceNs);
                    lock.lock();

                    // the queue may have been flushed while we weren't holding the lock
                    if (m_stop || m_restartPending || !ShouldPlay() || m_queue.empty())
                    {
                        continue;
                    }
                }

                if (m_params.type == StreamType::Buffered && static_cast<int64_t>(m_pcm->GetSize()) >= targetBytes)
                {
                    // enough audio in the pipe
                    m_cond.wait_for(lock, 10ms);
                }
                else
                {
                    AudioFrame frame = move(m_queue.front());
                    m_queue.pop_front();
                    m_condSpace.notify_all();

                    if (IsFlushed(frame.timestamp))
                    {
                        continue;
                    }
                    lock.unlock();
                    WriteFrame(frame);
                    lock.lock();

                    m_history.emplace_back(move(frame));

                    while (m_history.size() > MaxHistoryFrames)
                    {
                        m_history.pop_front();
                    }
                }

                if (m_pcm)
                {
                    const int64_t pending = static_cast<int64_t>(m_pcm->GetSize());
                    m_pendingData = pending;

                    if (!m_playAudio.valid() && (pending >= startFillBytes || m_queue.empty()))
                    {
                        m_playAudio = AlsaAudio::Play(m_pcm, audioDevice);
                        m_outputActive = true;
                    }
                }
            }
            catch (const exception& e)
            {
                spdlog::error("AirPlay2: player: {}", e.what());

                if (!lock.owns_lock())
                {
                    lock.lock();
                }
                m_cond.wait_for(lock, 100ms);
            }
        }
        lock.unlock();
        StopOutput();
    }
}
