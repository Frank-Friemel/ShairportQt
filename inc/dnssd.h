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
    class Service
    {
        public:
            Service( uint32_t interfaceIndex,
                        const char* serviceName,
                        const char* regtype,
                        const char* replyDomain)
                : m_interfaceIndex{ interfaceIndex }
                , m_serviceName{ serviceName ? serviceName : "" }
                , m_regtype{ regtype ? regtype : "" }
                , m_replyDomain{ replyDomain ? replyDomain : "" }
            {
            }

        public:
            const uint32_t m_interfaceIndex;
            const std::string m_serviceName;
            const std::string m_regtype;
            const std::string m_replyDomain;
    };

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
        void* handle,
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

    bool Succeeded() const noexcept;
    int ErrorCode() const noexcept;

    void* Handle() const noexcept
    {
        return m_handle;
    }

protected:
    const SharedPtr<DnsSD>      m_dnsSD;
    std::atomic_bool            m_stop{ false };
    void* const                 m_handle;
    const int32_t               m_error;
    std::future<void>           m_processResult;
};

using DnsHandlePtr = std::shared_ptr<DnsSDHandle>;

class DnsSD
    : public RefCount<>
{
    friend class DnsSDHandle;
private:
    class Descriptor;

protected:
    virtual ~DnsSD();

public:
    DnsSD();
    
    DnsHandlePtr CreateRaopServiceFromConfig(const SharedPtr<IValueCollection>& config, bool metaInfo);
    DnsHandlePtr BrowseForService(const char* strRegType, IDnsSDEvents* cb);
    DnsHandlePtr ResolveService(uint32_t interfaceIndex,
        const std::string& strService, const std::string& strRegType, const std::string& strReplyDomain, IDnsSDEvents* cb);
    DnsHandlePtr ServiceQueryRecord(uint32_t interfaceIndex, const std::string& fullname, IDnsSDEvents* cb);

 protected:
    const std::unique_ptr<Descriptor> m_descriptor;
};