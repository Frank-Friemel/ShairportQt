#include <dns_sd.h>
#include "dnssd.h"
#include <assert.h>
#include <stdexcept>
#include "LayerCake.h"
#include "libutils.h"
#include "Networking.h"
#include <set>

static uint8_t TxtLen(const char* txt) noexcept;

#ifdef _WIN32
#include <windns.h>
#pragma comment(lib, "dnsapi.lib")
#else
// LoadLibrary for Unix -> dlXXXX
#include <dlfcn.h>
#endif

#define RAOP_TXTVERS "1"
#define RAOP_CH "2"             // Audio channels: 2
#define RAOP_CN "0,1"           // Audio codec: PCM, ALAC
#define RAOP_ET "0,1"           // Encryption type: none, RSA
#define RAOP_SV "false"
#define RAOP_DA "true"
#define RAOP_SR "44100"
#define RAOP_SS "16"            // Sample size: 16
#define RAOP_VN "3"
#define RAOP_TP "UDP"
#define RAOP_MD "0,1,2"         // Metadata: text, artwork, progress
#define RAOP_NO_MD ""           // no Metadata
#define RAOP_SM "false"
#define RAOP_EK "1"
#define GLOBAL_VERSION  "130.14"

using namespace std;
using namespace string_literals;

#ifdef _WIN32

static void TXTRecordSetValue(list<wstring>& records, vector<const wchar_t*>& keys, vector<const wchar_t*>& values,
    const char* key, uint8_t, const char* value)
{
    records.push_back(CA2WEX(key));
    keys.push_back(records.back().c_str());
    records.push_back(CA2WEX(value));
    values.push_back(records.back().c_str());
}

static void SplitServiceDef(const char* name, string& serviceName, string& regtype, string& domain)
{
    // e.g. "HP Color LaserJet MFP M277dw (C162F4)._http._tcp.local"
    const char* c = name;
    int n = 0;

    while (*c)
    {
        if (*c == '.')
        {
            ++n;

            if (n != 2)
            {
                ++c;
                continue;
            }
        }
        switch (n)
        {
            case 0:
            {
                serviceName += *c;
            }
            break;

            case 1:
            case 2:
            {
                regtype += *c;
            }
            break;

            case 3:
            {
                domain += *c;
            }
            break;

            default:
            {
                assert(false);
            }
            break;
        }
        ++c;
    }

    if (domain.empty())
    {
        domain = ".local"s;
    }
    else
    {
        if (domain[0] != '.')
        {
            domain = "."s + domain;
        }
    }
}

static VOID WINAPI MyDnsServiceUnregisterCallback(DWORD, LPVOID, PDNS_SERVICE_INSTANCE);
static VOID WINAPI MyDnsServiceRegisterComplete(DWORD, PVOID, PDNS_SERVICE_INSTANCE);
static VOID WINAPI MyDnsServiceBrowseComplete(DWORD, PVOID, PDNS_RECORD);
static VOID WINAPI MyDnsServiceResolveComplete(DWORD, PVOID, PDNS_SERVICE_INSTANCE);

class DnsServiceContext;

static mutex mtxContextRegister;
static set<DnsServiceContext*> contextRegister;

class DnsServiceContext
{
protected:
    DnsServiceContext(IDnsSDEvents* cb = nullptr, wstring&& queryName = {})
        : callback{ cb }
        , m_queryName{ move(queryName) }
    {
        const lock_guard<mutex> guard{ mtxContextRegister };
        contextRegister.insert(this);
    }

public:
    virtual ~DnsServiceContext() = default;

    DnsServiceContext(const DnsServiceContext&) = delete;
    DnsServiceContext& operator=(const DnsServiceContext&) = delete;

public:    
    virtual void Destroy() noexcept
    {
        const lock_guard<mutex> guard{ mtxContextRegister };

        if (contextRegister.find(this) != contextRegister.end())
        {
            contextRegister.erase(this);
            delete this;
        }
    }

public:
    IDnsSDEvents* const callback;

protected:
    const wstring m_queryName;
    DNS_SERVICE_CANCEL m_cancel{};
};

class DnsRegisterServiceContext
    : public DnsServiceContext
{
public:
    DnsRegisterServiceContext(const wstring serviceName, uint16_t port, vector<const wchar_t*>& keys,
        vector<const wchar_t*>& values)
    {
        WCHAR hostName[8192];
        DWORD sizeHostname = 8192;
        if (!GetComputerNameW(hostName, &sizeHostname))
        {
            throw runtime_error("hostname is too long");
        }

        const auto computerName = wstring(hostName, sizeHostname) + L".local"s;

        assert(keys.size() == values.size());

        m_instance = DnsServiceConstructInstance(
            serviceName.c_str(),
            computerName.c_str(),
            nullptr,
            nullptr,
            port,
            0,
            0,
            static_cast<DWORD>(keys.size()),
            keys.data(),
            values.data()
        );

        if (!m_instance)
        {
            throw runtime_error("failed to construct dns service instance");
        }

        m_serviceRegisterRequest.Version = DNS_QUERY_REQUEST_VERSION1;
		m_serviceRegisterRequest.InterfaceIndex = 0;
		m_serviceRegisterRequest.pServiceInstance = m_instance;
		m_serviceRegisterRequest.pRegisterCompletionCallback = MyDnsServiceRegisterComplete;
		m_serviceRegisterRequest.pQueryContext = this;
		m_serviceRegisterRequest.unicastEnabled = false;

        const auto status = DnsServiceRegister(&m_serviceRegisterRequest, nullptr);

		if (status != DNS_REQUEST_PENDING)
        {
			throw runtime_error("failed to register dns service");
		}        
    }

    ~DnsRegisterServiceContext() override
    {
        if (m_instance)
        {
            DnsServiceFreeInstance(m_instance);
        }
        if (m_registeredInstance && m_registeredInstance != m_instance)
        {
            DnsServiceFreeInstance(m_registeredInstance);
        }
        m_registeredInstance = nullptr;
        m_instance = nullptr;
    }

    void Destroy() noexcept override
    {
        m_serviceRegisterRequest.pRegisterCompletionCallback = MyDnsServiceUnregisterCallback;
        
        const DWORD result = DnsServiceDeRegister(&m_serviceRegisterRequest, nullptr);
        assert(result == DNS_REQUEST_PENDING || result == ERROR_SUCCESS);
 
        if (result == ERROR_SUCCESS)
        {
            DnsServiceContext::Destroy();
        }
    }

    void SetRegisteredInstance(PDNS_SERVICE_INSTANCE instance) noexcept
    {
        assert(!m_registeredInstance);
        m_registeredInstance = instance;
        m_serviceRegisterRequest.pRegisterCompletionCallback = MyDnsServiceUnregisterCallback;
        m_serviceRegisterRequest.pServiceInstance = instance;
    }

protected:
    PDNS_SERVICE_INSTANCE m_instance = nullptr;
    PDNS_SERVICE_INSTANCE m_registeredInstance = nullptr;
    DNS_SERVICE_REGISTER_REQUEST m_serviceRegisterRequest{};
};

class DnsBrowseServiceContext
    : public DnsServiceContext
{
public:
    DnsBrowseServiceContext(IDnsSDEvents* cb, wstring&& queryName)
        : DnsServiceContext{ cb , move(queryName) }
    {
        m_serviceBrowseRequest.Version = DNS_QUERY_REQUEST_VERSION1;
        m_serviceBrowseRequest.InterfaceIndex = 0;
        m_serviceBrowseRequest.QueryName = m_queryName.c_str();
        m_serviceBrowseRequest.pBrowseCallback  = MyDnsServiceBrowseComplete;
        m_serviceBrowseRequest.pQueryContext = this;

        const auto status = DnsServiceBrowse(&m_serviceBrowseRequest, &m_cancel);

        if (status != DNS_REQUEST_PENDING)
        {
            throw runtime_error("failed to browse dns service");
        }        
    }

    void Destroy() noexcept override
    {
        const DWORD result = DnsServiceBrowseCancel(&m_cancel);
        assert(result == DNS_REQUEST_PENDING || result == ERROR_SUCCESS);

        if (result == ERROR_SUCCESS)
        {
            DnsServiceContext::Destroy();
        }
    }

    uint32_t GetInterfaceIndex() const noexcept
    {
        return m_serviceBrowseRequest.InterfaceIndex;
    }

protected:
    DNS_SERVICE_BROWSE_REQUEST  m_serviceBrowseRequest{};
};

class DnsResolveServiceContext
    : public DnsServiceContext
{
public:
    DnsResolveServiceContext(IDnsSDEvents* cb, wstring&& queryName, uint32_t interfaceIndex)
        : DnsServiceContext{ cb , move(queryName) }
    {
        m_serviceResolveRequest.Version = DNS_QUERY_REQUEST_VERSION1;
        m_serviceResolveRequest.InterfaceIndex = interfaceIndex;
        m_serviceResolveRequest.QueryName = (PWSTR)m_queryName.data();
        m_serviceResolveRequest.pResolveCompletionCallback = MyDnsServiceResolveComplete;
        m_serviceResolveRequest.pQueryContext = this;

        const auto status = DnsServiceResolve(&m_serviceResolveRequest, &m_cancel);

        if (status != DNS_REQUEST_PENDING)
        {
            throw runtime_error("failed to browse dns service");
        }
    }

    void Destroy() noexcept override
    {
        DWORD result = DnsServiceResolveCancel(&m_cancel);
        assert(result == DNS_REQUEST_PENDING || result == ERROR_SUCCESS);
 
        if (result == ERROR_SUCCESS)
        {
            DnsServiceContext::Destroy();
        }
    }

protected:
    DNS_SERVICE_RESOLVE_REQUEST m_serviceResolveRequest{};
};

static VOID WINAPI MyDnsServiceUnregisterCallback(DWORD, LPVOID context, PDNS_SERVICE_INSTANCE)
{
    assert(context);
    DnsServiceContext* dnsServiceContext = (DnsServiceContext*)context;
    const lock_guard<mutex> guard{ mtxContextRegister };

    if (contextRegister.find(dnsServiceContext) == contextRegister.end())
    {
        return;
    }
    contextRegister.erase(dnsServiceContext);
    delete dnsServiceContext;
}

static VOID WINAPI MyDnsServiceRegisterComplete(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance)
{
    assert(context);
    DnsServiceContext* dnsServiceContext = (DnsServiceContext*)context;
    {
        const lock_guard<mutex> guard{ mtxContextRegister };

        if (contextRegister.find(dnsServiceContext) == contextRegister.end())
        {
            return;
        }
    }

    if (status == ERROR_SUCCESS)
    {
        ((DnsRegisterServiceContext*)dnsServiceContext)->SetRegisteredInstance(instance);
    }
    else
    {
        // unexpected
        assert(false);
        const lock_guard<mutex> guard{ mtxContextRegister };
        contextRegister.erase(dnsServiceContext);
        delete dnsServiceContext;
    }
}

static VOID WINAPI MyDnsServiceBrowseComplete(DWORD status, PVOID context, PDNS_RECORD records)
{
    assert(context);
    const ScopeContext cleanup{ [&records]() {
        if (records)
        {
            DnsRecordListFree(records, DnsFreeRecordList);
        }
    }};

    DnsServiceContext* dnsServiceContext = (DnsServiceContext*)context;
    {
        const lock_guard<mutex> guard{ mtxContextRegister };

        if (contextRegister.find(dnsServiceContext) == contextRegister.end())
        {
            return;
        }
        if (status != ERROR_SUCCESS)
        {
            if (status == ERROR_CANCELLED)
            {
                contextRegister.erase(dnsServiceContext);
                delete dnsServiceContext;
            }
            return;
        }
    }

    const uint32_t interfaceIndex = ((DnsBrowseServiceContext*)dnsServiceContext)->GetInterfaceIndex();

    for (auto record = records; record; record = record->pNext)
    {
		if (record->wType == DNS_TYPE_PTR)
        {
            try
            {
                string serviceName;
                string regtype;
                string replyDomain;
                string name = CW2AEX(wstring((const wchar_t*)(record->Data.PTR.pNameHost)));

                SplitServiceDef(name.c_str(), serviceName, regtype, replyDomain);
                
                const bool registered = record->dwTtl > 0 ? true : false;

                assert(dnsServiceContext->callback);
                dnsServiceContext->callback->OnDNSServiceBrowseReply(registered, interfaceIndex,
                    serviceName.c_str(), regtype.c_str(), replyDomain.c_str());
            }
            catch(...)
            {
            }
		}
	}
}
    
static VOID WINAPI MyDnsServiceResolveComplete(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance)
{
    assert(context);
    const ScopeContext cleanup{ [&instance]() {
        if (instance)
        {
            DnsServiceFreeInstance(instance);
        }
    }};
    DnsServiceContext* dnsServiceContext = (DnsServiceContext*)context;
    {
        const lock_guard<mutex> guard{ mtxContextRegister };

        if (contextRegister.find(dnsServiceContext) == contextRegister.end())
        {
            return;
        }
        if (status != ERROR_SUCCESS)
        {
            if (status == ERROR_CANCELLED)
            {
                contextRegister.erase(dnsServiceContext);
                delete dnsServiceContext;
            }
            return;
        }
    }

    try
    {
        assert(dnsServiceContext->callback);
        dnsServiceContext->callback->OnServiceResolved(context, nullptr, 0, CW2AEX(wstring(instance->pszHostName)).c_str(),
                CW2AEX(wstring(instance->pszInstanceName)).c_str(), SWAP16(instance->wPort));
    }
    catch(...)
    {
    }
}

#endif // _WIN32

class DnsSD::Descriptor
{
private:
	typedef DNSServiceErrorType (DNSSD_API *_typeDNSServiceRegister)
                                                (
                                                    DNSServiceRef*                      sdRef,
                                                    DNSServiceFlags                     flags,
                                                    uint32_t                            interfaceIndex,
                                                    const char*                         name,         /* may be NULL */
                                                    const char*                         regtype,
                                                    const char*                         domain,       /* may be NULL */
                                                    const char*                         host,         /* may be NULL */
                                                    uint16_t                            port,
                                                    uint16_t                            txtLen,
                                                    const void*                         txtRecord,    /* may be NULL */
                                                    DNSServiceRegisterReply             callBack,     /* may be NULL */
                                                    void*                               context       /* may be NULL */
                                                );
    typedef void (DNSSD_API *_typeDNSServiceRefDeallocate)(DNSServiceRef sdRef);
    typedef int (DNSSD_API *_typeDNSServiceRefSockFD)(DNSServiceRef sdRef);
	typedef DNSServiceErrorType (DNSSD_API *_typeDNSServiceProcessResult)(DNSServiceRef sdRef);
	typedef DNSServiceErrorType (DNSSD_API *_typeDNSServiceQueryRecord)
												(
                                                    DNSServiceRef                       *sdRef,
                                                    DNSServiceFlags                     flags,
                                                    uint32_t                            interfaceIndex,
                                                    const char                          *fullname,
                                                    uint16_t	                        rrtype,
                                                    uint16_t							rrclass,
                                                    DNSServiceQueryRecordReply          callBack,
                                                    void                                *context  
												);
	typedef DNSServiceErrorType (DNSSD_API *_typeDNSServiceResolve)
												(
                                                    DNSServiceRef*                      sdRef,
                                                    DNSServiceFlags                     flags,
                                                    uint32_t                            interfaceIndex,
                                                    const char*                         name,
                                                    const char*                         regtype,
                                                    const char*                         domain,
                                                    DNSServiceResolveReply              callBack,
                                                    void*                               context  
												);
    typedef DNSServiceErrorType (DNSSD_API *_typeDNSServiceBrowse)
                                                (
                                                    DNSServiceRef *sdRef,
                                                    DNSServiceFlags                     flags,
                                                    uint32_t                            interfaceIndex,
                                                    const char*                         regtype,
                                                    const char*                         domain,    /* may be NULL */
                                                    DNSServiceBrowseReply               callBack,
                                                    void*                               context    /* may be NULL */
                                                );
    typedef void (DNSSD_API *_typeTXTRecordCreate)
                                                (
                                                    TXTRecordRef     *txtRecord,
                                                    uint16_t         bufferLen,
                                                    void             *buffer
                                                );
    typedef DNSServiceErrorType (DNSSD_API *_typeTXTRecordSetValue)
                                                (
                                                    TXTRecordRef     *txtRecord,
                                                    const char       *key,
                                                    uint8_t          valueSize,        /* may be zero */
                                                    const void       *value            /* may be NULL */
                                                );
    typedef uint16_t (DNSSD_API *_typeTXTRecordGetLength)
                                                (
                                                    const TXTRecordRef *txtRecord
                                                );
    typedef const void * (DNSSD_API *_typeTXTRecordGetBytesPtr)
                                                (
                                                    const TXTRecordRef *txtRecord
                                                );
    typedef void (DNSSD_API *_typeTXTRecordDeallocate)
                                                (
                                                    TXTRecordRef     *txtRecord
                                                );

#ifndef _WIN32
    typedef void* HMODULE;

    static void* GetProcAddress(HMODULE module, const char* name)
    {
        return dlsym((void*)module, name);
    }
#endif

public:
    Descriptor()
    {
        m_module = nullptr;
        Load();
    }

    ~Descriptor()
    {
        Unload();
    }

    bool IsValid() const noexcept
    {
        return !!m_module;
    }
    
    bool Load()
    {
        if (IsValid())
        {
            return true;
        }
#ifdef _WIN32
        m_module = LoadLibraryA("dnssd.dll");

        if (!m_module)
        {
            // Apple Bonjour is optional on Win32
            return false;
        }
#else
	    m_module = dlopen("libdns_sd.so", RTLD_LAZY);

        if (!m_module)
        {
            m_module = dlopen("libdns_sd.so.1", RTLD_LAZY);
        }
#endif   

        if (!m_module)
        {
            throw runtime_error("Could not load dnssd shared library");
	    }

        m_funcDNSServiceRegister        = (_typeDNSServiceRegister) GetProcAddress(m_module, "DNSServiceRegister");
	    m_funcDNSServiceRefDeallocate   = (_typeDNSServiceRefDeallocate) GetProcAddress(m_module, "DNSServiceRefDeallocate");
        m_funcDNSServiceRefSockFD       = (_typeDNSServiceRefSockFD) GetProcAddress(m_module, "DNSServiceRefSockFD");
        m_funcDNSServiceProcessResult   = (_typeDNSServiceProcessResult) GetProcAddress(m_module, "DNSServiceProcessResult");
	    m_funcDNSServiceQueryRecord     = (_typeDNSServiceQueryRecord) GetProcAddress(m_module, "DNSServiceQueryRecord");
	    m_funcDNSServiceResolve         = (_typeDNSServiceResolve) GetProcAddress(m_module, "DNSServiceResolve");
	    m_funcDNSServiceBrowse          = (_typeDNSServiceBrowse) GetProcAddress(m_module, "DNSServiceBrowse");
	    m_funcTXTRecordCreate           = (_typeTXTRecordCreate) GetProcAddress(m_module, "TXTRecordCreate");
	    m_funcTXTRecordSetValue         = (_typeTXTRecordSetValue) GetProcAddress(m_module, "TXTRecordSetValue");
	    m_funcTXTRecordGetLength        = (_typeTXTRecordGetLength) GetProcAddress(m_module, "TXTRecordGetLength");
	    m_funcTXTRecordGetBytesPtr      = (_typeTXTRecordGetBytesPtr) GetProcAddress(m_module, "TXTRecordGetBytesPtr");
	    m_funcTXTRecordDeallocate       = (_typeTXTRecordDeallocate) GetProcAddress(m_module, "TXTRecordDeallocate");

        if (!m_funcDNSServiceRegister || !m_funcDNSServiceRefDeallocate || !m_funcDNSServiceRefSockFD ||
            !m_funcDNSServiceProcessResult || !m_funcDNSServiceQueryRecord || !m_funcDNSServiceResolve ||
            !m_funcDNSServiceBrowse || !m_funcTXTRecordCreate || !m_funcTXTRecordSetValue || !m_funcTXTRecordGetLength ||
            !m_funcTXTRecordGetBytesPtr || !m_funcTXTRecordDeallocate) 
        {
            Unload();
            throw runtime_error("Could not load dnssd shared library functions");
        }
        return true;
    }

    void Unload() noexcept
    {
        HMODULE module = nullptr;

        swap(module, m_module);

        if (module)
        {
#ifdef _WIN32
            if (!FreeLibrary(module))
            {
                assert(false);
            }
#else
            if (0 != dlclose(module))
            {
                assert(false);
            }
#endif   
        }
    }

public:
    HMODULE m_module = nullptr;

    _typeDNSServiceRegister         m_funcDNSServiceRegister        = nullptr;
    _typeDNSServiceRefDeallocate    m_funcDNSServiceRefDeallocate   = nullptr;
    _typeDNSServiceRefSockFD        m_funcDNSServiceRefSockFD       = nullptr;
    _typeDNSServiceProcessResult    m_funcDNSServiceProcessResult   = nullptr;
    _typeDNSServiceQueryRecord      m_funcDNSServiceQueryRecord     = nullptr;
    _typeDNSServiceResolve          m_funcDNSServiceResolve         = nullptr;
	_typeDNSServiceBrowse           m_funcDNSServiceBrowse          = nullptr;
    _typeTXTRecordCreate            m_funcTXTRecordCreate           = nullptr;
	_typeTXTRecordSetValue          m_funcTXTRecordSetValue         = nullptr;
	_typeTXTRecordGetLength         m_funcTXTRecordGetLength        = nullptr;
	_typeTXTRecordGetBytesPtr       m_funcTXTRecordGetBytesPtr      = nullptr;
	_typeTXTRecordDeallocate        m_funcTXTRecordDeallocate       = nullptr;
};

DnsSD::DnsSD(bool forceNative)
    : m_descriptor{ make_unique<Descriptor>() }
#ifdef _WIN32    
    , m_forceNative{ forceNative || !m_descriptor->IsValid() }
#endif
{
}

DnsSD::~DnsSD()
{
}

DnsHandlePtr DnsSD::CreateRaopServiceFromConfig(const SharedPtr<IValueCollection>& config, bool metaInfo)
{
    const bool      hasPassword = VariantValue::Key("HasPassword").Get<bool>(config) &&
                                !VariantValue::Key("Password").Get<string>(config).empty();
    const auto      hwAddr      = VariantValue::Key("HWaddress").Get<vector<uint8_t>>(config);
    const uint16_t  port        = VariantValue::Key("RaopPort").Get<uint16_t>(config);
    const wstring   name        = CA2WEX(EncodeToHex(hwAddr, true)) + L"@"s + VariantValue::Key("APname").Get<wstring>(config);
    const wstring   regType     = L"_raop._tcp"s;

#ifdef _WIN32
    if (m_forceNative)
    {
        // use native DnsSD
        list<wstring> records;
        vector<const wchar_t*> keys;
        vector<const wchar_t*> values;

        TXTRecordSetValue(records, keys, values, "txtvers", TxtLen(RAOP_TXTVERS), RAOP_TXTVERS);
        TXTRecordSetValue(records, keys, values, "ch", TxtLen(RAOP_CH), RAOP_CH);
        TXTRecordSetValue(records, keys, values, "cn", TxtLen(RAOP_CN), RAOP_CN);
        TXTRecordSetValue(records, keys, values, "et", TxtLen(RAOP_ET), RAOP_ET);
        TXTRecordSetValue(records, keys, values, "sv", TxtLen(RAOP_SV), RAOP_SV);

        if (!hasPassword)
        {
            TXTRecordSetValue(records, keys, values, "da", TxtLen(RAOP_DA), RAOP_DA);
        }
        TXTRecordSetValue(records, keys, values, "sr", TxtLen(RAOP_SR), RAOP_SR);
        TXTRecordSetValue(records, keys, values, "ss", TxtLen(RAOP_SS), RAOP_SS);
        
        if (hasPassword) 
        {
            TXTRecordSetValue(records, keys, values, "pw", TxtLen("true"), "true");
        } 
        else 
        {
            TXTRecordSetValue(records, keys, values, "pw", TxtLen("false"), "false");
        }
        TXTRecordSetValue(records, keys, values, "vn", TxtLen(RAOP_VN), RAOP_VN);
        TXTRecordSetValue(records, keys, values, "tp", TxtLen(RAOP_TP), RAOP_TP);

        if (!metaInfo)
        {
            TXTRecordSetValue(records, keys, values, "md", TxtLen(RAOP_NO_MD), RAOP_NO_MD);
        }
        else
        {
            TXTRecordSetValue(records, keys, values, "md", TxtLen(RAOP_MD), RAOP_MD);
        }
        if (!hasPassword)
        {
            TXTRecordSetValue(records, keys, values, "vs", TxtLen(GLOBAL_VERSION), GLOBAL_VERSION);
        }
        TXTRecordSetValue(records, keys, values, "sm", TxtLen(RAOP_SM), RAOP_SM);
        TXTRecordSetValue(records, keys, values, "ek", TxtLen(RAOP_EK), RAOP_EK);
 
        const wstring serviceName = name + L"."s + regType + L".local"s;
    
        return make_shared<DnsSDHandle>(this, new DnsRegisterServiceContext(serviceName, port, keys, values));
    }
#endif

    TXTRecordRef txtRecord;

    m_descriptor->m_funcTXTRecordCreate(&txtRecord, 0, NULL);
    m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "txtvers", TxtLen(RAOP_TXTVERS), RAOP_TXTVERS);
    m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "ch", TxtLen(RAOP_CH), RAOP_CH);
    m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "cn", TxtLen(RAOP_CN), RAOP_CN);
    m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "et", TxtLen(RAOP_ET), RAOP_ET);
    m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "sv", TxtLen(RAOP_SV), RAOP_SV);

    if (!hasPassword)
    {
        m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "da", TxtLen(RAOP_DA), RAOP_DA);
    }
    m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "sr", TxtLen(RAOP_SR), RAOP_SR);
    m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "ss", TxtLen(RAOP_SS), RAOP_SS);
    
    if (hasPassword) 
    {
        m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "pw", TxtLen("true"), "true");
    } 
    else 
    {
        m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "pw", TxtLen("false"), "false");
    }
    m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "vn", TxtLen(RAOP_VN), RAOP_VN);
    m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "tp", TxtLen(RAOP_TP), RAOP_TP);

    if (!metaInfo)
    {
        m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "md", TxtLen(RAOP_NO_MD), RAOP_NO_MD);
    }
    else
    {
        m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "md", TxtLen(RAOP_MD), RAOP_MD);
    }
    if (!hasPassword)
    {
        m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "vs", TxtLen(GLOBAL_VERSION), GLOBAL_VERSION);
    }
    m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "sm", TxtLen(RAOP_SM), RAOP_SM);
    m_descriptor->m_funcTXTRecordSetValue(&txtRecord, "ek", TxtLen(RAOP_EK), RAOP_EK);

    DNSServiceRef sdRef = nullptr;

    const auto error = m_descriptor->m_funcDNSServiceRegister(&sdRef, 0, kDNSServiceInterfaceIndexAny, CW2AEX(name).c_str()
        , CW2AEX(regType).c_str(), NULL, NULL, SWAP16(port), 
        m_descriptor->m_funcTXTRecordGetLength(&txtRecord), 
        m_descriptor->m_funcTXTRecordGetBytesPtr(&txtRecord), nullptr, nullptr);

    m_descriptor->m_funcTXTRecordDeallocate(&txtRecord);

    return make_shared<DnsSDHandle>(this, static_cast<void*>(sdRef), static_cast<int32_t>(error));
}

static void DNSSD_API MyDNSServiceBrowseReply
(
    DNSServiceRef                       sdRef,
    DNSServiceFlags                     flags,
    uint32_t                            interfaceIndex,
    DNSServiceErrorType                 errorCode,
    const char*                         serviceName,
    const char*                         regtype,
    const char*                         replyDomain,
    void*                               context
)
{
    assert(context);
    IDnsSDEvents* cb = (IDnsSDEvents*)context;

    const bool registered = (flags & kDNSServiceFlagsAdd) ? true : false;

    cb->OnDNSServiceBrowseReply(registered, interfaceIndex, serviceName, regtype, replyDomain);
}

DnsHandlePtr DnsSD::BrowseForService(const char* strRegType, IDnsSDEvents* cb)
{
    assert(cb);

#ifdef _WIN32  
    if (m_forceNative)
    {
        // use native DnsSD
        wstring queryName = CA2WEX(string(strRegType)) + L".local"s;
        return make_shared<DnsSDHandle>(this, new DnsBrowseServiceContext(cb, move(queryName)));
    }
#endif
    DNSServiceRef sdRef = nullptr;

    const auto error = m_descriptor->m_funcDNSServiceBrowse(&sdRef, 0, 0, strRegType, NULL, MyDNSServiceBrowseReply, cb);

    return make_shared<DnsSDHandle>(this, static_cast<void*>(sdRef), static_cast<int32_t>(error));
}

static void DNSSD_API MyDNSServiceResolveReply
(
    DNSServiceRef                       sdRef,
    DNSServiceFlags                     flags,
    uint32_t                            interfaceIndex,
    DNSServiceErrorType                 errorCode,
    const char*                         fullname,
    const char*                         hosttarget,
    uint16_t                            port,
    uint16_t                            txtLen,
    const unsigned char*                txtRecord,
    void*                               context
)
{
    assert(context);
    IDnsSDEvents* cb = (IDnsSDEvents*)context;

    cb->OnServiceResolved(sdRef, txtRecord, txtLen, hosttarget, fullname, port);
}

DnsHandlePtr DnsSD::ResolveService(uint32_t interfaceIndex,
    const string& strService, const string& strRegType, const string& strReplyDomain, IDnsSDEvents* cb)
{
#ifdef _WIN32  
    if (m_forceNative)
    {
        // use native DnsSD
        string domain;

        if (!strReplyDomain.empty())
        {
            if (strReplyDomain[0] != '.')
            {
                domain = "."s;
            }
            domain += strReplyDomain;
        }
        else
        {
            domain = ".local"s;
        }

        wstring queryName = CA2WEX(strService + "."s + strRegType + domain);
        return make_shared<DnsSDHandle>(this, new DnsResolveServiceContext(cb, move(queryName), interfaceIndex));
    }
#endif    
    DNSServiceRef sdRef = nullptr;

    const auto error = m_descriptor->m_funcDNSServiceResolve(&sdRef, 0, interfaceIndex, strService.c_str()
        , strRegType.c_str(), strReplyDomain.c_str()
        , MyDNSServiceResolveReply, cb);
 
    return make_shared<DnsSDHandle>(this, static_cast<void*>(sdRef), static_cast<int32_t>(error));
}

bool DnsSD::UsesAppleBonjour() const noexcept
{
#ifdef _WIN32
    if (m_forceNative)
    {
        return false;
    }
#endif
    return m_descriptor->IsValid();
}

bool DnsSD::SetForceNative(bool forceNative) noexcept
{
#ifdef _WIN32    
    if (m_forceNative.load() != forceNative)
    {
        m_forceNative = forceNative;
        
        if (m_forceNative)
        {
            m_descriptor->Unload();
        }
        else
        {
            try
            {
                if (m_descriptor->Load())
                {
                    return true;
                }
            }
            catch(...)
            {
            }
            // we can not switch to Bonjour, if it's not available
            // so we stick to the system native dnsSD
            m_forceNative = true;
            return false;
        }
    }
#endif
    return true;
}

///////////////////////////////////////////////////////////////////////
// DnsSDHandle

DnsSDHandle::DnsSDHandle(SharedPtr<DnsSD> dnsSD, void* handle /* = nullptr */, int error /* =0 */)
    : m_dnsSD{ move(dnsSD) }
    , m_handle{ handle }
    , m_error{ error }
{
    assert(m_dnsSD);

    if (m_handle && static_cast<DNSServiceErrorType>(m_error) == kDNSServiceErr_NoError)
    {
        if (m_dnsSD->UsesAppleBonjour())
        {
            m_processResult = async(launch::async, [this]() -> void {
                if (m_stop)
                {
                    return;
                }
                const int socket = m_dnsSD->m_descriptor->m_funcDNSServiceRefSockFD(static_cast<DNSServiceRef>(m_handle));
                assert(socket != -1);

                if (!Networking::SetSocketBlockingEnabled(socket, false))
                {
                    // unexpected if we don't stop!
                    assert(m_stop);
                }

                for (;;)
                {
                    if (m_stop)
                    {
                        return;
                    }
                    const int dataAvailable = Networking::WaitForIncomingData(socket);

                    if (m_stop || dataAvailable < 0)
                    {
                        return;
                    }
                    if (dataAvailable)
                    {
                        if (kDNSServiceErr_NoError != m_dnsSD->m_descriptor->m_funcDNSServiceProcessResult(static_cast<DNSServiceRef>(m_handle)))
                        {
                            return;
                        }
                    }
                }
            });
        }
    }
}

DnsSDHandle::~DnsSDHandle()
{
    m_stop = true;

    if (static_cast<DNSServiceErrorType>(m_error) == kDNSServiceErr_NoError)
    {
        if (m_dnsSD->UsesAppleBonjour())
        {
            assert(m_handle);
            m_dnsSD->m_descriptor->m_funcDNSServiceRefDeallocate(static_cast<DNSServiceRef>(m_handle));
            
            if (m_processResult.valid())
            {
                try
                {
                    m_processResult.wait();
                }
                catch (...)
                {
                    assert(false);
                }
            }
        }
        else
        {
#ifdef _WIN32
            DnsServiceContext* context = (DnsServiceContext*)m_handle;
            assert(context);
            context->Destroy();
#endif           
        }
    }
}

bool DnsSDHandle::Succeeded() const noexcept
{
    return m_error == kDNSServiceErr_NoError;
}

int DnsSDHandle::ErrorCode() const noexcept
{
    return m_error;
}

static uint8_t TxtLen(const char* txt) noexcept
{
    const size_t l = strlen(txt);
    assert(l < 256);

    return static_cast<uint8_t>(l);
}

