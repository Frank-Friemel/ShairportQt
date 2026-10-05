#include "airplay2/PtpClock.h"
#include "sockpp/udp_socket.h"
#include "sockpp/inet_address.h"
#include <spdlog/spdlog.h>
#include <chrono>

using namespace std;

namespace AirPlay2
{
    int64_t LocalTimeNs() noexcept
    {
        return chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now().time_since_epoch()).count();
    }

    namespace
    {
        uint64_t ReadBE(const uint8_t* p, size_t n) noexcept
        {
            uint64_t v = 0;
            for (size_t i = 0; i < n; ++i)
            {
                v = (v << 8) | p[i];
            }
            return v;
        }

        constexpr size_t PtpHeaderSize = 34;
        constexpr int64_t StaleClockNs = 30'000'000'000LL;
    }

    bool PtpMessage::Parse(const uint8_t* data, size_t len, PtpMessage& msg) noexcept
    {
        if (len < PtpHeaderSize)
        {
            return false;
        }
        if ((data[1] & 0x0f) != 2)
        {
            // PTP version 2 only
            return false;
        }
        msg.type = data[0] & 0x0f;
        msg.twoStep = (data[6] & 0x02) != 0;
        msg.correctionNs = static_cast<int64_t>(ReadBE(data + 8, 8)) / 65536;
        msg.clockId = ReadBE(data + 20, 8);
        msg.sequenceId = static_cast<uint16_t>(ReadBE(data + 30, 2));

        if (msg.type == 0x0 || msg.type == 0x8 || msg.type == 0xB)
        {
            if (len < PtpHeaderSize + 10)
            {
                return false;
            }
            const uint64_t seconds = ReadBE(data + 34, 6);
            const uint64_t nanos = ReadBE(data + 40, 4);
            msg.originNs = static_cast<int64_t>(seconds * 1'000'000'000ULL + nanos);
        }
        if (msg.type == 0xB)
        {
            // Announce: grandmasterIdentity at offset 53
            if (len < 61)
            {
                return false;
            }
            msg.grandmasterId = ReadBE(data + 53, 8);
        }
        return true;
    }

    PtpClock::PtpClock()
    {
        sockpp::socket_initializer::initialize();

        try
        {
            auto eventSocket = make_unique<sockpp::udp_socket>();
            auto generalSocket = make_unique<sockpp::udp_socket>();

            if (!eventSocket->bind(sockpp::inet_address(319)) || !generalSocket->bind(sockpp::inet_address(320)))
            {
                spdlog::warn("AirPlay2: can't bind PTP ports 319/320 - falling back to local timing (see doc/AirPlay2-OptionalFeatures.md)");
                return;
            }
#ifdef _WIN32
            const DWORD timeout = 500;
            eventSocket->set_option(SOL_SOCKET, SO_RCVTIMEO, static_cast<const void*>(&timeout), static_cast<socklen_t>(sizeof(timeout)));
            generalSocket->set_option(SOL_SOCKET, SO_RCVTIMEO, static_cast<const void*>(&timeout), static_cast<socklen_t>(sizeof(timeout)));
#else
            timeval tv{ 0, 500000 };
            eventSocket->set_option(SOL_SOCKET, SO_RCVTIMEO, tv);
            generalSocket->set_option(SOL_SOCKET, SO_RCVTIMEO, tv);
#endif
            m_eventSocket = move(eventSocket);
            m_generalSocket = move(generalSocket);
            m_listening = true;

            m_threads.emplace_back([this]() { Run(m_eventSocket.get(), true); });
            m_threads.emplace_back([this]() { Run(m_generalSocket.get(), false); });
            spdlog::info("AirPlay2: listening for PTP on ports 319/320");
        }
        catch (const exception& e)
        {
            spdlog::warn("AirPlay2: PTP listener failed: {}", e.what());
        }
    }

    PtpClock::~PtpClock()
    {
        m_stop = true;

        for (auto* s : { m_eventSocket.get(), m_generalSocket.get() })
        {
            if (s)
            {
                try
                {
                    s->shutdown();
                }
                catch (...)
                {
                }
            }
        }
        for (auto& t : m_threads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
    }

    void PtpClock::Run(sockpp::udp_socket* socket, bool isEventPort) noexcept
    {
        uint8_t buf[512];

        while (!m_stop)
        {
            try
            {
                const auto n = socket->recv(buf, sizeof(buf));
                const int64_t now = LocalTimeNs();

                if (n <= 0)
                {
                    continue;
                }
                PtpMessage msg;

                if (PtpMessage::Parse(buf, static_cast<size_t>(n), msg))
                {
                    // Sync is sent to the event port, Follow_Up & Announce to the general port
                    if ((msg.type == 0x0) == isEventPort)
                    {
                        OnMessage(msg, now);
                    }
                }
            }
            catch (...)
            {
            }
        }
    }

    void PtpClock::ApplySample(ClockState& state, int64_t networkNs, int64_t localNs)
    {
        const int64_t sample = networkNs - localNs;

        // network delays only make the sample smaller,
        // so we follow increases immediately and decreases slowly
        if (!state.hasOffset || localNs - state.lastUpdateLocalNs > StaleClockNs)
        {
            state.offsetNs = sample;
            state.hasOffset = true;
        }
        else if (sample > state.offsetNs)
        {
            state.offsetNs = sample;
        }
        else
        {
            state.offsetNs += (sample - state.offsetNs) / 16;
        }
        state.lastUpdateLocalNs = localNs;
    }

    void PtpClock::OnMessage(const PtpMessage& msg, int64_t receivedLocalNs)
    {
        const lock_guard<mutex> guard(m_mtx);
        auto& state = m_clocks[msg.clockId];

        if (msg.type == 0x0)
        {
            if (msg.twoStep)
            {
                state.syncSeq = msg.sequenceId;
                state.syncLocalNs = receivedLocalNs;
                state.syncPending = true;
            }
            else
            {
                ApplySample(state, msg.originNs + msg.correctionNs, receivedLocalNs);
            }
        }
        else if (msg.type == 0x8)
        {
            if (state.syncPending && state.syncSeq == msg.sequenceId)
            {
                state.syncPending = false;
                ApplySample(state, msg.originNs + msg.correctionNs, state.syncLocalNs);
            }
        }
    }

    bool PtpClock::GetOffset(uint64_t clockId, int64_t& offsetNs) const
    {
        const lock_guard<mutex> guard(m_mtx);
        const int64_t now = LocalTimeNs();

        if (clockId)
        {
            const auto i = m_clocks.find(clockId);

            if (i != m_clocks.end() && i->second.hasOffset && now - i->second.lastUpdateLocalNs < StaleClockNs)
            {
                offsetNs = i->second.offsetNs;
                return true;
            }
        }
        // fall back to the most recently updated clock
        const ClockState* best = nullptr;

        for (const auto& c : m_clocks)
        {
            if (c.second.hasOffset && now - c.second.lastUpdateLocalNs < StaleClockNs &&
                (!best || c.second.lastUpdateLocalNs > best->lastUpdateLocalNs))
            {
                best = &c.second;
            }
        }
        if (best)
        {
            offsetNs = best->offsetNs;
            return true;
        }
        return false;
    }

    bool PtpClock::NetworkToLocal(uint64_t clockId, int64_t networkNs, int64_t& localNs) const
    {
        int64_t offset = 0;

        if (!GetOffset(clockId, offset))
        {
            return false;
        }
        localNs = networkNs - offset;
        return true;
    }
}
