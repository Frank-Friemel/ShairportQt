#include "DacpService.h"
#include <assert.h>
#include <spdlog/spdlog.h>
#include "libutils.h"
#include "Trim.h"

using namespace std;
using namespace literals;

DacpService::DacpService(const SharedPtr<DnsSD>& dnsSD, uint32_t interfaceIndex, std::string&& serviceName,
	string&& regType, string&& replyDomain)
	: m_dnsSD{ dnsSD }
	, m_interfaceIndex{ interfaceIndex  }
	, m_serviceName{ move(serviceName) }
	, m_regType{ move(regType) }
	, m_replyDomain{ move(replyDomain) }

{
    assert(m_dnsSD.IsValid());

    // we construct the full name by chance
    m_fullName = m_serviceName + "."s + m_regType + m_replyDomain;

    Resolve();
}

DacpService::~DacpService()
{
    if (m_futureResolveServiceHandle.valid())
    {
        try
        {
            const auto handle = m_futureResolveServiceHandle.get();
        }
        catch (...)
        {
            assert(false);
        }
    }
}

bool DacpService::IsResolved() const noexcept
{
    return m_resolved;
}

uint16_t DacpService::GetPort(unsigned int waitMS /*= 0*/) const noexcept
{
    unique_lock<mutex> sync(m_mtx);
    m_condResolved.WaitAndLock(sync, [this, waitMS]() { return m_resolved.load() || waitMS == 0; }, waitMS);
    return m_port;
}

void DacpService::Resolve()
{
    if (m_resolved)
    {
        const lock_guard<mutex> guard(m_mtx);
        m_hostName.clear();
        m_port      = 0;
        m_resolved  = false;
    }
    auto futureResolveServiceHandle = async(launch::async, [this]() -> DnsHandlePtr
        {
            return m_dnsSD->ResolveService(m_interfaceIndex, m_serviceName, m_regType, m_replyDomain, this);
        });
    {
        const lock_guard<mutex> guard(m_mtx);
        std::swap(futureResolveServiceHandle, m_futureResolveServiceHandle);
    }
}

void DacpService::OnServiceResolved(
    const unsigned char* txtrecord,
    uint16_t txtLen,
    const char* hosttarget,
    const char* fullname,
    uint16_t port) noexcept
{
    try
    {
        string hostTarget   = hosttarget ? hosttarget : ""s;
        string fullName     = fullname ? fullname : ""s;
        string hostName     = hostTarget;

        // try calculate the host name by chance
        Trim(hostName, "."s);

        spdlog::info("resolved service {} to: {}, {} - Fullname: {}", m_serviceName, hostTarget, port, fullName);

        {
            unique_lock<mutex> sync(m_mtx);

            m_port          = port;
            m_hostTarget    = move(hostTarget);
            m_fullName      = move(fullName);

            // take over host name (if not set yet)
            if (m_hostName.empty())
            {
                m_hostName = move(hostName);
            }
            m_resolved = true;
            m_condResolved.NotifyAndUnlock(sync, Condition::mode::all);
        }
    }
    catch (const exception& e)
    {
        spdlog::error("could not resolve service {}: {}", m_serviceName, e.what());
    }
}
