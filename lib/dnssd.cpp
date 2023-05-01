#include <dns_sd.h>
#include "dnssd.h"
#include <assert.h>
#include <stdexcept>
#include "LayerCake.h"
#include "libutils.h"
#include "Networking.h"

static uint8_t TxtLen(const char* txt);
static char* DnsParseDomainName(char* p, char** x) noexcept;

#ifndef _WIN32
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
                                                    DNSServiceRegisterReply             callBack,      /* may be NULL */
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
#ifdef _WIN32
        m_module = LoadLibraryA("dnssd.dll");
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
    }

    ~Descriptor()
    {
        Unload();
    }

private:
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

DnsSD::DnsSD()
    : m_descriptor{ make_unique<Descriptor>() }
{
}

DnsSD::~DnsSD()
{
}

DnsHandlePtr DnsSD::CreateRaopServiceFromConfig(const SharedPtr<IValueCollection>& config, bool metaInfo)
{
    const bool hasPassword = VariantValue::Key("HasPassword").Get<bool>(config) &&
                                !VariantValue::Key("Password").Get<string>(config).empty();
    const auto hwAddr = VariantValue::Key("HWaddress").Get<vector<uint8_t>>(config);
    const auto apName = VariantValue::Key("APname").Get<string>(config);
    uint16_t port = SWAP16(VariantValue::Key("RaopPort").Get<uint16_t>(config));

    string name = EncodeToHex(hwAddr, true);
    
    name += "@";
    name += apName;

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

	const auto error = m_descriptor->m_funcDNSServiceRegister(&sdRef, 0, kDNSServiceInterfaceIndexAny, name.c_str()
        , "_raop._tcp", NULL, NULL, port, 
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

    cb->OnServiceResolved(txtRecord, txtLen, hosttarget, fullname, port);
}

DnsHandlePtr DnsSD::ResolveService(uint32_t interfaceIndex,
    const string& strService, const string& strRegType, const string& strReplyDomain, IDnsSDEvents* cb)
{
    DNSServiceRef sdRef = nullptr;

    const auto error = m_descriptor->m_funcDNSServiceResolve(&sdRef, 0, interfaceIndex, strService.c_str()
        , strRegType.c_str(), strReplyDomain.c_str()
        , MyDNSServiceResolveReply, cb);
 
    return make_shared<DnsSDHandle>(this, static_cast<void*>(sdRef), static_cast<int32_t>(error));
}

static void DNSSD_API MyDNSServiceQueryRecordReply
(
    DNSServiceRef                       sdRef,
    DNSServiceFlags                     flags,
    uint32_t                            interfaceIndex,
    DNSServiceErrorType                 errorCode,
    const char*                         fullname,
    uint16_t                            rrtype,
    uint16_t                            rrclass,
    uint16_t                            rdlen,
    const void*                         rdata,
    uint32_t                            ttl,
    void*                               context
)
{
    assert(context);
    IDnsSDEvents* cb = (IDnsSDEvents*)context;

    string host;
    
    switch (rrtype)
    {
    case kDNSServiceType_A:
    {
        struct my_sockaddr_in
        {
            int16_t     sin_family;
            uint16_t    sin_port;
            struct  
            {
                uint8_t s_b1, s_b2, s_b3, s_b4;
            }           sin_addr;
            int8_t      sin_zero[8];
        };
        struct my_sockaddr_in sa4{};

        if (sizeof(sa4) >= rdlen)
        {
            memcpy(&sa4.sin_addr, rdata, rdlen);

            try
            {
                host = 
                    to_string(sa4.sin_addr.s_b1) + "."s +
                    to_string(sa4.sin_addr.s_b2) + "."s +
                    to_string(sa4.sin_addr.s_b3) + "."s +
                    to_string(sa4.sin_addr.s_b4);
            }
            catch (...)
            {
            }
        }
    }
    break;

    case kDNSServiceType_SRV:
    {
        char* rd = (char*)malloc(rdlen);

        if (rd)
        {
            memcpy(rd, rdata, rdlen);

            char* x = rd + 3 * sizeof(uint16_t);
            char* name = DnsParseDomainName(x, &x);

            if (name)
            {
                try
                {
                    host = name;
                }
                catch (...)
                {
                }
                free(name);
            }
            free(rd);
        }
    }
    break;
    }

    cb->OnServiceQueryRecord(host);
}

DnsHandlePtr DnsSD::ServiceQueryRecord(uint32_t interfaceIndex, const string& fullname, IDnsSDEvents* cb)
{
    DNSServiceRef sdRef = nullptr;

    const auto error = m_descriptor->m_funcDNSServiceQueryRecord(&sdRef, 0, interfaceIndex, fullname.c_str(),
        kDNSServiceType_SRV, kDNSServiceClass_IN, MyDNSServiceQueryRecordReply, cb);
    
    return make_shared<DnsSDHandle>(this, static_cast<void*>(sdRef), static_cast<int32_t>(error));
}

///////////////////////////////////////////////////////////////////////
// DnsSDHandle

DnsSDHandle::DnsSDHandle(SharedPtr<DnsSD> dnsSD, void* handle /* = nullptr */, int error /* =0 */)
    : m_dnsSD{ move(dnsSD) }
{
    assert(m_dnsSD);
    Init(handle, error);
}

void DnsSDHandle::Init(void* h, int32_t e)
{
    m_handle = h;
    m_error = e;
        
    if (m_handle && static_cast<DNSServiceErrorType>(m_error) == kDNSServiceErr_NoError)
    {
        m_processResult = async(launch::async, [this]() -> void
            {
                int socket = m_dnsSD->m_descriptor->m_funcDNSServiceRefSockFD(static_cast<DNSServiceRef>(m_handle));

                Networking::SetSocketBlockingEnabled(socket, false);

                DNSServiceErrorType err;

                do
                {
                    if (m_stop)
                    {
                        break;
                    }
                    const int selected = Networking::WaitForIncomingData(socket);

                    if (m_stop || selected < 0)
                    {
                        break;
                    }
                    if (select == 0)
                    {
                        continue;
                    }
                    err = m_dnsSD->m_descriptor->m_funcDNSServiceProcessResult(static_cast<DNSServiceRef>(m_handle));
                } while (err == kDNSServiceErr_NoError);
            });
    }
}

DnsSDHandle::~DnsSDHandle()
{
    m_stop = true;

    if (static_cast<DNSServiceErrorType>(m_error) == kDNSServiceErr_NoError)
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
}

static uint8_t TxtLen(const char* txt)
{
    const size_t l = strlen(txt);
    assert(l < 256);

    return static_cast<uint8_t>(l);
}

static char* DnsParseDomainName(char* p, char** x) noexcept
{
    uint8_t* v8;
    uint16_t* v16, skip;
    uint16_t i, j, dlen, len;
    int more, compressed;
    char* name, * start;

    start = *x;
    compressed = 0;
    more = 1;
    name = (char*)malloc(1);

    if (!name)
    {
        return nullptr;
    }
    name[0] = '\0';
    len = 1;
    j = 0;
    skip = 0;

    while (more == 1)
    {
        v8 = (uint8_t*)*x;
        dlen = *v8;

        if ((dlen & 0xc0) == 0xc0)
        {
            v16 = (uint16_t*)*x;
            *x = p + (SWAP16(*v16) & 0x3fff);
            if (compressed == 0) skip += 2;
            compressed = 1;
            continue;
        }

        *x += 1;
        if (dlen > 0)
        {
            len += dlen;
            name = (char*)realloc(name, len);
            if (!name)
            {
                return nullptr;
            }
        }

        for (i = 0; i < dlen; i++)
        {
            name[j++] = **x;
            *x += 1;
        }
        name[j] = '\0';
        if (compressed == 0) skip += (dlen + 1);

        if (dlen == 0) more = 0;
        else
        {
            v8 = (uint8_t*)*x;
            if (*v8 != 0)
            {
                len += 1;
                name = (char*)realloc(name, len);
                if (!name)
                {
                    return nullptr;
                }
                name[j++] = '.';
                name[j] = '\0';
            }
        }
    }

    *x = start + skip;

    return name;
}
