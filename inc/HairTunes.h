#pragma once

#include "LayerCake.h"
#include <vector>
#include <stdint.h>
#include <thread>
#include "Condition.h"
#include <atomic>
#include "RaopEndpoint.h"
#include <list>
#include "crypto.h"

namespace alac
{
    struct alac_file;
};

class HairTunes
    : public IRtpRequestHandler
{
public:
    HairTunes(const SharedPtr<IValueCollection> config, const SharedPtr<IValueCollection>& client);
    ~HairTunes();

    unsigned int GetServerPort() const noexcept;
    unsigned int GetControlPort() const noexcept;
    unsigned int GetTimingPort() const noexcept;

    void Flush();

    const std::string& GetClientID() const noexcept;

    void ResetProgess() noexcept;
    int GetProgressTime() const noexcept;
    bool IsPlaying() const noexcept;
    uint64_t GetSamplingFreq() const noexcept;

protected:
    void OnRequest(RtpEndpoint* endpoint, std::unique_ptr<RtpPacket>&& packet) override;
        
private:

    void RunQueue() noexcept;

    void QueuePacket(std::unique_ptr<RtpPacket>&& p, bool isResendPacket);
    	
    void RequestResend(const USHORT nSeq, const short nCount) noexcept;
    bool AsyncRequestResend(const std::unique_lock<std::mutex>& sync, const USHORT nSeq, const short nCount) noexcept;

    void AlacDecode(std::unique_ptr<RtpPacket>& packet);

private:
    class ResendRequest
    {
    public:
        ResendRequest(const uint16_t _seq, const short _count, std::future<void>&& _async)
            : seq{ _seq }
            , count { _count }
            , async { std::move(_async) }
        {
        }

    public:
        const uint16_t              seq;
        const short                 count;
        const std::future<void>     async;
    };
    using ResendRequestPtr = std::unique_ptr<ResendRequest>;

	const SharedPtr<IValueCollection>       m_config;
    const SharedPtr<IValueCollection>       m_client;

    const std::string                       m_clientID;
    const int                               m_remoteControlPort;

    int                                     m_frameBytes;
    int                                     m_samplingRate;
    
    alac::alac_file*                        m_decoder = nullptr;
    
    std::atomic_bool                        m_mute;

    std::unique_ptr<std::thread>            m_queueThread;

    std::mutex                              m_mtxQueue;
    Condition                               m_condQueue;
    std::atomic_bool                        m_stopThread;
    std::atomic_uint                        m_flush;
        
    std::unique_ptr<RtpEndpoint>            m_controlEndpoint;
    std::unique_ptr<RtpEndpoint>            m_dataEndpoint;
    std::unique_ptr<RtpEndpoint>            m_timingEndpoint;

    std::list<std::unique_ptr<RtpPacket>>   m_packetQueue;
    std::list<ResendRequestPtr>             m_asyncResend;

    const size_t                            m_lowLevelQueue;
    const size_t                            m_highLevelQueue;
    
    Crypto::Aes                             m_aes;
    const std::vector<uint8_t>              m_iv;

    std::atomic_int64_t                     m_progressData;
    std::atomic_int64_t                     m_pendingData;

    std::atomic_bool                        m_isPlaying{ false };
};