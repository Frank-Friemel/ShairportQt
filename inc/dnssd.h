#pragma once

#include <memory>
#include <string>
#include "LayerCake.h"
#include <future>
#include <assert.h>
#include <functional>
#include <atomic>

class IDnsSDEvents
{
public:
    virtual void OnDNSServiceBrowseReply(
        bool registered,
        uint32_t interfaceIndex,
        const char* serviceName,
        const char* regtype,
        const char* replyDomain) noexcept
    {
        // override when you call BrowseForService
        assert(false);
    }

    virtual void OnServiceResolved(
        const unsigned char* txtRecord,
        uint16_t txtLen,
        const char* hosttarget,
        const char* fullname,
        uint16_t port) noexcept
    {
        // override when you call ResolveService
        assert(false);
    }

    virtual void OnServiceQueryRecord(const std::string& host) noexcept
    {
        // override when you call ServiceQueryRecord
        assert(false);
    }
};

class DnsSD;

class DnsSDHandle
{
public:
    DnsSDHandle(SharedPtr<DnsSD> dnsSD, void* handle = nullptr, int32_t error = 0);
    ~DnsSDHandle();

    bool Succeeded() const noexcept
    {
        return m_error == 0;
    }

    int ErrorCode() const noexcept
    {
        return m_error;
    }

protected:
    void Init(void* handle, int32_t error);

protected:
    const SharedPtr<DnsSD>      m_dnsSD;
    std::atomic_bool            m_stop{ false };
    void*                       m_handle;
    int32_t                     m_error;
    std::future<void>           m_processResult;
};

using DnsHandlePtr = std::shared_ptr<DnsSDHandle>;

class DnsSD
    : public RefCount<>
{
    friend class DnsSDHandle;
private:
    class Descriptor;

public:
    DnsSD();
    ~DnsSD();
    
    DnsHandlePtr CreateRaopServiceFromConfig(const SharedPtr<IValueCollection>& config, bool metaInfo);
    DnsHandlePtr BrowseForService(const char* strRegType, IDnsSDEvents* cb);
    DnsHandlePtr ResolveService(uint32_t interfaceIndex,
        const std::string& strService, const std::string& strRegType, const std::string& strReplyDomain, IDnsSDEvents* cb);
    DnsHandlePtr ServiceQueryRecord(uint32_t interfaceIndex, const std::string& fullname, IDnsSDEvents* cb);

 protected:
    const std::unique_ptr<Descriptor> m_descriptor;
};