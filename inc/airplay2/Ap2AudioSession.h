#pragma once

#include "IAudioSession.h"
#include "LayerCake.h"
#include "RaopEndpoint.h"
#include "airplay2/Ap2Crypto.h"
#include "airplay2/Ap2Decoder.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "sockpp/tcp_acceptor.h"

namespace AirPlay2
{
    class PtpClock;

    enum class StreamType : int
    {
        Realtime = 96,
        Buffered = 103,
    };

    // one decrypted (but still encoded) audio packet
    struct AudioFrame
    {
        uint32_t    seq{ 0 };
        uint32_t    timestamp{ 0 };
        uint32_t    ssrc{ 0 };
        Bytes       payload;
    };

    // decrypts an AirPlay 2 audio packet:
    // [4 seq/flags][4 timestamp][4 ssrc][ciphertext][16 tag][8 nonce] (AAD = timestamp + ssrc)
    bool DecryptAudioPacket(const uint8_t* packet, size_t len, const Bytes& key, AudioFrame& frame);

    struct Ap2SessionParams
    {
        StreamType  type{ StreamType::Buffered };
        AudioFormat format;             // initial / realtime format
        Bytes       sharedKey;          // "shk" (32 bytes)
        std::string clientID;           // remote address of the sender
    };

    class Ap2AudioSession
        : public IAudioSession
        , public IRtpRequestHandler
    {
    public:
        Ap2AudioSession(SharedPtr<IValueCollection> config, Ap2SessionParams params, std::shared_ptr<PtpClock> clock);
        ~Ap2AudioSession() override;

        Ap2AudioSession(const Ap2AudioSession&) = delete;
        Ap2AudioSession& operator=(const Ap2AudioSession&) = delete;

        uint16_t GetDataPort() const noexcept { return m_dataPort; }
        uint16_t GetControlPort() const noexcept;
        static constexpr uint64_t AudioBufferSize = 8 * 1024 * 1024;

        // SETRATEANCHORTIME
        void SetRateAnchor(double rate, std::optional<uint32_t> rtpTime, std::optional<int64_t> networkTimeNs, uint64_t clockId);

        // FLUSHBUFFERED
        void FlushBuffered(std::optional<uint32_t> fromTimestamp, uint32_t untilTimestamp);

        // stops the session (TEARDOWN of the stream)
        void Stop() noexcept;

        // IAudioSession
        void Flush(unsigned int seq = 0) noexcept override;
        const std::string& GetClientID() const noexcept override { return m_params.clientID; }
        void ResetProgess() noexcept override;
        int GetProgressTime() const noexcept override;
        bool IsPlaying() const noexcept override;
        uint64_t GetSamplingFreq() const noexcept override;

        // feeds a raw (encrypted) packet - exposed for testing
        void OnPacket(const uint8_t* data, size_t len);

    protected:
        // IRtpRequestHandler (realtime data and control port)
        void OnRequest(RtpEndpoint* endpoint, std::unique_ptr<RtpPacket>&& packet) override;

    private:
        void RunBufferedReceiver() noexcept;
        void ReadBufferedConnection(sockpp::tcp_socket& socket) noexcept;
        void RunPlayer() noexcept;

        void EnqueueFrame(AudioFrame&& frame);
        bool ShouldPlay() const noexcept;
        bool IsFlushed(uint32_t ts) const noexcept;

        // player thread helpers (called without holding m_mtx)
        bool StartOutput();
        void StopOutput() noexcept;
        void WriteSilence(int64_t durationNs);
        void WriteFrame(const AudioFrame& frame);

    private:
        const SharedPtr<IValueCollection>   m_config;
        const Ap2SessionParams              m_params;
        const std::shared_ptr<PtpClock>     m_clock;

        std::unique_ptr<RtpEndpoint>        m_controlEndpoint;
        std::unique_ptr<RtpEndpoint>        m_realtimeEndpoint;
        std::unique_ptr<sockpp::tcp_acceptor> m_acceptor;
        uint16_t                            m_dataPort{ 0 };

        std::atomic_bool                    m_stop{ false };
        std::thread                         m_receiverThread;
        std::thread                         m_playerThread;

        mutable std::mutex                  m_mtx;
        std::condition_variable             m_cond;
        std::condition_variable             m_condSpace;
        std::deque<AudioFrame>              m_queue;
        std::deque<AudioFrame>              m_history;

        // playback state (guarded by m_mtx)
        double                              m_rate{ 0. };
        bool                                m_anchorValid{ false };
        uint32_t                            m_anchorRtp{ 0 };
        std::optional<int64_t>              m_anchorLocalNs;
        bool                                m_restartPending{ false };
        std::optional<uint32_t>             m_flushFrom;
        std::optional<uint32_t>             m_flushUntil;

        // output (player thread only)
        SharedPtr<BlobStream>               m_pcm;
        std::future<int>                    m_playAudio;
        std::unique_ptr<Ap2Decoder>         m_decoder;
        bool                                m_skipNextBlock{ false };
        bool                                m_firstFrameDecoded{ false };
        bool                                m_unknownSsrcLogged{ false };
        double                              m_errLeft{ 0. };
        double                              m_errRight{ 0. };

        std::atomic_bool                    m_outputActive{ false };
        std::atomic_int64_t                 m_progressData{ 0 };
        std::atomic_int64_t                 m_pendingData{ 0 };
    };
}
