#pragma once

#include <string>
#include <stdint.h>
#include <future>
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>
#include "dnssd.h"
#include "LayerCake.h"
#include "Condition.h"

class DacpService
    : public IDnsSDEvents
{
public:
	DacpService(const SharedPtr<DnsSD>& dnsSD, uint32_t interfaceIndex, std::string&& serviceName,
		std::string&& regType, std::string&& replyDomain);

	DacpService(const DacpService& i) = delete;
	DacpService& operator=(const DacpService& i) = delete;

    ~DacpService();

    bool IsResolved() const noexcept;
    uint16_t GetPort(uint32_t waitMS = 0) const noexcept;

public:
    void Resolve();

protected:
    // implemenation of IDnsSDEvents
    void OnServiceResolved(
        const unsigned char* txtRecord,
        uint16_t txtLen,
        const char* hosttarget,
        const char* fullname,
        uint16_t port) noexcept override;

private:
	const SharedPtr<DnsSD>	            m_dnsSD;
    const uint32_t			            m_interfaceIndex{ 0 };
	const std::string		            m_serviceName;
	const std::string		            m_regType;
	const std::string		            m_replyDomain;
    mutable std::mutex                  m_mtx;
    std::atomic_bool                    m_resolved{ false };
    mutable Condition                   m_condResolved;
    std::string                         m_hostTarget;
    std::string                         m_hostName;
    std::string                         m_fullName;
    uint16_t                            m_port{ 0 };
    mutable std::future<DnsHandlePtr>   m_futureResolveServiceHandle;
};

using DacpServicePtr = std::shared_ptr<DacpService>;