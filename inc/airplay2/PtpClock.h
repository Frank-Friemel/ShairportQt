#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "sockpp/udp_socket.h"

namespace AirPlay2
{
    // local monotonic time in nanoseconds
    int64_t LocalTimeNs() noexcept;

    // Parsed fields of a PTP (IEEE 1588v2) message which we need
    struct PtpMessage
    {
        uint8_t     type{ 0 };          // 0 = Sync, 8 = Follow_Up, 0xB = Announce
        bool        twoStep{ false };
        uint64_t    clockId{ 0 };       // source clock identity
        uint16_t    sequenceId{ 0 };
        int64_t     correctionNs{ 0 };
        int64_t     originNs{ 0 };      // origin / precise origin timestamp in ns
        uint64_t    grandmasterId{ 0 }; // only for Announce

        static bool Parse(const uint8_t* data, size_t len, PtpMessage& msg) noexcept;
    };

    // Very small passive PTP slave: listens for Sync/Follow_Up messages of the
    // sender's (grandmaster) clock on the UDP ports 319/320 and estimates the offset
    // between the network clock and our local monotonic clock.
    // Binding the ports may fail (e.g. privileged ports on Linux, or another PTP daemon running).
    // In this case the clock is "not available" and callers have to fall back.
    class PtpClock
    {
    public:
        PtpClock();
        ~PtpClock();

        PtpClock(const PtpClock&) = delete;
        PtpClock& operator=(const PtpClock&) = delete;

        bool IsListening() const noexcept { return m_listening; }

        // offset = network time - local time [ns] for the given clock (0 = any recent clock)
        bool GetOffset(uint64_t clockId, int64_t& offsetNs) const;

        // converts a network time of the given clock to local time, false if not possible
        bool NetworkToLocal(uint64_t clockId, int64_t networkNs, int64_t& localNs) const;

        // feed a received message (exposed for unit testing)
        void OnMessage(const PtpMessage& msg, int64_t receivedLocalNs);

    private:
        void Run(sockpp::udp_socket* socket, bool isEventPort) noexcept;

        struct ClockState
        {
            uint16_t    syncSeq{ 0 };
            int64_t     syncLocalNs{ 0 };
            bool        syncPending{ false };
            bool        hasOffset{ false };
            int64_t     offsetNs{ 0 };
            int64_t     lastUpdateLocalNs{ 0 };
        };

        void ApplySample(ClockState& state, int64_t networkNs, int64_t localNs);

        mutable std::mutex                          m_mtx;
        std::map<uint64_t, ClockState>              m_clocks;
        std::atomic_bool                            m_stop{ false };
        std::atomic_bool                            m_listening{ false };
        std::unique_ptr<sockpp::udp_socket>         m_eventSocket;
        std::unique_ptr<sockpp::udp_socket>         m_generalSocket;
        std::vector<std::thread>                    m_threads;
    };
}
