#pragma once

#include <cstdio>
#include <ctype.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <memory.h>
#include <locale>
#include <string>
#include <codecvt>
#include <memory>
#include <atomic>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <list>
#include <map>
#include <functional>
#include <future>
#include <thread>
#include <condition_variable>
#include <optional>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN 1

#include <windows.h>
#include <objidl.h>
#include <ocidl.h>

typedef VARIANT* PVARIANT;
typedef const VARIANT* PCVARIANT;

#endif

#ifndef CONST
#define CONST               const
#endif

#ifndef LOWORD
#define LOWORD(_dw)     ((WORD)((_dw) & 0xffff))
#endif
#ifndef HIWORD
#define HIWORD(_dw)     ((WORD)(((_dw) >> 16) & 0xffff))
#endif
#ifndef LODWORD
#define LODWORD(_qw)    ((DWORD)(_qw))
#endif
#ifndef HIDWORD
#define HIDWORD(_qw)    ((DWORD)(((_qw) >> 32) & 0xffffffff))
#endif

#ifndef LOBYTE
#define LOBYTE(w)           ((BYTE)((w) & 0xff))
#endif

#ifndef HIBYTE
#define HIBYTE(w)           ((BYTE)(((w) >> 8) & 0xff))
#endif

#ifndef MAKEUINT64
#define MAKEUINT64(__lo, __hi) ((uint64_t(__hi) << 32) | ((__lo) & 0xffffffff))
#endif

#ifndef MAKEUINT16
#define MAKEUINT16(__lo, __hi) ((uint16_t)(((uint8_t)(((uint16_t)(__lo)) & 0xff)) | ((uint16_t)((uint8_t)(((uint16_t)(__hi)) & 0xff))) << 8))
#endif

#ifndef MAKEUINT32
#define MAKEUINT32(__lo, __hi) ((uint32_t)(((uint16_t)(((uint32_t)(__lo)) & 0xffff)) | ((uint32_t)((uint16_t)(((uint32_t)(__hi)) & 0xffff))) << 16))
#endif

#ifndef CP_UTF8
#define CP_UTF8 65001
#endif

#ifndef CP_UTF16
#define CP_UTF16 65002
#endif


inline uint32_t SWAP32(const uint32_t v)
{
    return (((v) & 0x000000FF) << 0x18) |
        (((v) & 0x0000FF00) << 0x08) |
        (((v) & 0x00FF0000) >> 0x08) |
        (((v) & 0xFF000000) >> 0x18);
}

inline uint16_t SWAP16(const uint16_t v)
{
    return (((v) & 0x00FF) << 0x08) |
        (((v) & 0xFF00) >> 0x08);
}

#ifndef _WIN32

inline std::wstring CA2WEX(const std::string& input, unsigned int cp = CP_UTF8)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(input);
}

inline std::string CW2AEX(const std::wstring& input, unsigned int cp = CP_UTF8)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.to_bytes(input);
}

inline uint32_t GetLastError() noexcept
{
    return static_cast<uint32_t>(errno);
}

inline uint32_t GetEnvironmentVariableA(const char* lpName, char* lpBuffer, uint32_t nSize)
{
    uint32_t result = 0;
    char* env = getenv(lpName);

    if (env)
    {
        result = static_cast<uint32_t>(strlen(env));

        if (result && lpBuffer && nSize >= result)
        {
            memcpy(lpBuffer, env, result);
        }
    }
    return result;
}

bool GetComputerNameA(char* lpBuffer, uint32_t* nSize);

bool DeleteFileA(const char* lpFileName);
bool MoveFileA(const char* lpExistingFileName, const char* lpNewFileName);

#else

std::wstring CA2WEX(const std::string& input, unsigned int cp = CP_UTF8);
std::string CW2AEX(const std::wstring& input, unsigned int cp = CP_UTF8);

#endif

inline char GetPathDelimiter()
{
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

inline char GetPathSeparator()
{
    return GetPathDelimiter();
}

char GetFloatingPointSeparator();

template<class T>
inline T AppendPathDelimiter(const T& path)
{
    const typename T::value_type del = static_cast<const typename T::value_type>(GetPathDelimiter());

    if (path.empty() || path[path.length() - 1] != del)
    {
        return path + del;
    }
    return path;
}

#ifndef _WIN32

#define STDMETHODCALLTYPE 

typedef struct _GUID {
    uint32_t  Data1;
    uint16_t Data2;
    uint16_t Data3;
    unsigned char  Data4[8];
} GUID;

typedef char                CHAR;
typedef void*               PVOID;
typedef void*               HANDLE;
typedef const void*         PCVOID;
typedef uint32_t            DWORD;
typedef int                 BOOL;
typedef uint8_t             BYTE;
typedef float               FLOAT;
typedef FLOAT*              PFLOAT;
typedef BOOL*               PBOOL;
typedef BOOL*               LPBOOL;
typedef BYTE*               PBYTE;
typedef BYTE*               LPBYTE;
typedef int*                PINT;
typedef int*                LPINT;
typedef int16_t             WORD;
typedef WORD*               PWORD;
typedef WORD*               LPWORD;
typedef int32_t*            LPLONG;
typedef DWORD*              PDWORD;
typedef DWORD*              LPDWORD;
typedef void*               LPVOID;
typedef CONST void *        LPCVOID;
typedef uint32_t            ULONG;
typedef int32_t             LONG;
typedef int16_t             SHORT;
typedef uint16_t            USHORT;
typedef LONG                SCODE;
typedef int64_t             LONGLONG;
typedef uint64_t            ULONGLONG;
typedef double              DOUBLE;
typedef int16_t             VARIANT_BOOL;
typedef double              DATE;
typedef wchar_t*            BSTR;
typedef char                CCHAR;
typedef DWORD               LCID;
typedef PDWORD              PLCID;
typedef WORD                LANGID;
typedef LONG                HRESULT;
typedef GUID                IID;
typedef wchar_t             WCHAR;
typedef WCHAR               OLECHAR;
typedef OLECHAR*            LPOLESTR;
typedef const OLECHAR*      LPCOLESTR;
typedef OLECHAR*            POLESTR;
typedef const OLECHAR*      PCOLESTR; 
typedef LONG                DISPID;
typedef DISPID              MEMBERID;
typedef DWORD               HREFTYPE;
typedef const wchar_t*      PCWSTR;
typedef const char*         PCSTR;
typedef wchar_t*            PWSTR;
typedef char*               PSTR; 
typedef HANDLE              HGLOBAL;

#ifndef MAXDWORD
#define MAXDWORD            0xFFFFFFFF
#endif

#define VARIANT_TRUE        ((VARIANT_BOOL)-1)
#define VARIANT_FALSE       ((VARIANT_BOOL)0)

#define S_OK                ((HRESULT)0L)
#define S_FALSE             ((HRESULT)1L)

#define SEVERITY_SUCCESS    0
#define SEVERITY_ERROR      1

#define VARIANT_NOVALUEPROP      0x01
#define VARIANT_ALPHABOOL        0x02 // For VT_BOOL to VT_BSTR conversions,
// convert to "True"/"False" instead of
// "-1"/"0"
#define VARIANT_NOUSEROVERRIDE   0x04 // For conversions to/from VT_BSTR,
                      // passes LOCALE_NOUSEROVERRIDE
                      // to core coercion routines
#define VARIANT_CALENDAR_HIJRI   0x08
#define VARIANT_LOCALBOOL        0x10 // For VT_BOOL to VT_BSTR and back,
                                      // convert to local language rather than
                                      // English
#define VARIANT_CALENDAR_THAI		0x20  // SOUTHASIA calendar support
#define VARIANT_CALENDAR_GREGORIAN	0x40  // SOUTHASIA calendar support
#define VARIANT_USE_NLS             0x80  // NLS function call support

// Compare results.  These are returned as a SUCCESS HResult.  Subtracting
// one gives the usual values of -1 for Less Than, 0 for Equal To, +1 for
// Greater Than.
//
#define VARCMP_LT   0
#define VARCMP_EQ   1
#define VARCMP_GT   2
#define VARCMP_NULL 3

#define LOCALE_USER_DEFAULT         0x00

//
// Generic test for success on any status value (non-negative numbers
// indicate success).
//

#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)

//
// and the inverse
//

#define FAILED(hr) (((HRESULT)(hr)) < 0)


//
// Generic test for error on any status value.
//

#define IS_ERROR(Status) (((uint32_t)(Status)) >> 31 == SEVERITY_ERROR)

//
// Return the code
//

#define HRESULT_CODE(hr)    ((hr) & 0xFFFF)
#define SCODE_CODE(sc)      ((sc) & 0xFFFF)

//
//  Return the facility
//

#define HRESULT_FACILITY(hr)  (((hr) >> 16) & 0x1fff)
#define SCODE_FACILITY(sc)    (((sc) >> 16) & 0x1fff)

//
//  Return the severity
//

#define HRESULT_SEVERITY(hr)  (((hr) >> 31) & 0x1)
#define SCODE_SEVERITY(sc)    (((sc) >> 31) & 0x1)

//
// Create an HRESULT value from component pieces
//

#define MAKE_HRESULT(sev,fac,code) \
    ((HRESULT) (((uint32_t)(sev)<<31) | ((uint32_t)(fac)<<16) | ((uint32_t)(code))) )
#define MAKE_SCODE(sev,fac,code) \
    ((SCODE) (((uint32_t)(sev)<<31) | ((uint32_t)(fac)<<16) | ((uint32_t)(code))) )


//
// Map a WIN32 error value into a HRESULT
// Note: This assumes that WIN32 errors fall in the range -32k to 32k.
//
// Define bits here so macros are guaranteed to work

#define FACILITY_NT_BIT                 0x10000000
#define FACILITY_WIN32                  7

//
// HRESULT_FROM_WIN32(x) used to be a macro, however we now run it as an inline function
// to prevent double evaluation of 'x'. If you still need the macro, you can use __HRESULT_FROM_WIN32(x)
//
#define __HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? ((HRESULT)(x)) : ((HRESULT) (((x) & 0x0000FFFF) | (FACILITY_WIN32 << 16) | 0x80000000)))
#define _HRESULT_TYPEDEF_(_sc) ((HRESULT)_sc)

#define E_NOTIMPL                       _HRESULT_TYPEDEF_(0x80004001L)
#define E_OUTOFMEMORY                   _HRESULT_TYPEDEF_(0x8007000EL)
#define E_INVALIDARG                    _HRESULT_TYPEDEF_(0x80070057L)
#define E_NOINTERFACE                   _HRESULT_TYPEDEF_(0x80004002L)
#define E_FAIL                          _HRESULT_TYPEDEF_(0x80004005L)
#define STG_E_INVALIDPOINTER            _HRESULT_TYPEDEF_(0x80030009L)
#define STG_E_NOMOREFILES               _HRESULT_TYPEDEF_(0x80030012L)
#define STG_E_SEEKERROR                 _HRESULT_TYPEDEF_(0x80030019L)
#define STG_E_WRITEFAULT                _HRESULT_TYPEDEF_(0x8003001DL)
#define STG_E_READFAULT                 _HRESULT_TYPEDEF_(0x8003001EL)
#define STG_E_SHAREVIOLATION            _HRESULT_TYPEDEF_(0x80030020L)
#define ERROR_SUCCESS                   0L
#define ERROR_FILE_NOT_FOUND            2L
#define ERROR_INVALID_HANDLE            6L
#define ERROR_HANDLE_EOF                38L
#define ERROR_HANDLE_DISK_FULL          39L
#define ERROR_NOT_SUPPORTED             50L
#define ERROR_IO_INCOMPLETE             996L

#define REFIID                          const IID &
#define REFGUID                         const GUID &

typedef uint16_t                  VARTYPE;

#define MAXLONGLONG                     (0x7fffffffffffffff)
#define MAXLONG                         (0x7fffffff)

#ifndef NULL
#define NULL                            ((void *)0)
#endif

#ifndef FALSE
#define FALSE                           0
#endif

#ifndef TRUE
#define TRUE                            1
#endif

typedef union tagCY {
    struct {
        ULONG   Lo;
        LONG    Hi;
    } DUMMYSTRUCTNAME;
    LONGLONG int64;
} CY;

typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    } u;
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef LARGE_INTEGER* PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
    struct {
        DWORD LowPart;
        DWORD HighPart;
    } u;
    ULONGLONG QuadPart;
} ULARGE_INTEGER;

typedef ULARGE_INTEGER* PULARGE_INTEGER;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, * PFILETIME, * LPFILETIME;

typedef GUID IID;

extern const IID GUID_NULL;

inline int InlineIsEqualGUID(REFGUID rguid1, REFGUID rguid2)
{
    return (
        ((uint32_t*)&rguid1)[0] == ((uint32_t*)&rguid2)[0] &&
        ((uint32_t*)&rguid1)[1] == ((uint32_t*)&rguid2)[1] &&
        ((uint32_t*)&rguid1)[2] == ((uint32_t*)&rguid2)[2] &&
        ((uint32_t*)&rguid1)[3] == ((uint32_t*)&rguid2)[3]);
}

inline int IsEqualGUID(REFGUID rguid1, REFGUID rguid2)
{
    return !memcmp(&rguid1, &rguid2, sizeof(GUID));
}

typedef IID* LPIID;
#define IID_NULL            GUID_NULL
#define IsEqualIID(riid1, riid2) IsEqualGUID(riid1, riid2)
typedef GUID CLSID;
typedef CLSID* LPCLSID;
#define CLSID_NULL          GUID_NULL
#define IsEqualCLSID(rclsid1, rclsid2) IsEqualGUID(rclsid1, rclsid2)

/*
 * VARENUM usage key,
 *
 * * [V] - may appear in a VARIANT
 * * [T] - may appear in a TYPEDESC
 * * [P] - may appear in an OLE property set
 * * [S] - may appear in a Safe Array
 *
 *
 *  VT_EMPTY            [V]   [P]     nothing
 *  VT_NULL             [V]   [P]     SQL style Null
 *  VT_I2               [V][T][P][S]  2 byte signed int
 *  VT_I4               [V][T][P][S]  4 byte signed int
 *  VT_R4               [V][T][P][S]  4 byte real
 *  VT_R8               [V][T][P][S]  8 byte real
 *  VT_CY               [V][T][P][S]  currency
 *  VT_DATE             [V][T][P][S]  date
 *  VT_BSTR             [V][T][P][S]  OLE Automation string
 *  VT_DISPATCH         [V][T]   [S]  IDispatch *
 *  VT_ERROR            [V][T][P][S]  SCODE
 *  VT_BOOL             [V][T][P][S]  True=-1, False=0
 *  VT_VARIANT          [V][T][P][S]  VARIANT *
 *  VT_UNKNOWN          [V][T]   [S]  IUnknown *
 *  VT_DECIMAL          [V][T]   [S]  16 byte fixed point
 *  VT_RECORD           [V]   [P][S]  user defined type
 *  VT_I1               [V][T][P][s]  signed char
 *  VT_UI1              [V][T][P][S]  unsigned char
 *  VT_UI2              [V][T][P][S]  uint16_t
 *  VT_UI4              [V][T][P][S]  ULONG
 *  VT_I8                  [T][P]     signed 64-bit int
 *  VT_UI8                 [T][P]     unsigned 64-bit int
 *  VT_INT              [V][T][P][S]  signed machine int
 *  VT_UINT             [V][T]   [S]  unsigned machine int
 *  VT_INT_PTR             [T]        signed machine register size width
 *  VT_UINT_PTR            [T]        unsigned machine register size width
 *  VT_VOID                [T]        C style void
 *  VT_HRESULT             [T]        Standard return type
 *  VT_PTR                 [T]        pointer type
 *  VT_SAFEARRAY           [T]        (use VT_ARRAY in VARIANT)
 *  VT_CARRAY              [T]        C style array
 *  VT_USERDEFINED         [T]        user defined type
 *  VT_LPSTR               [T][P]     null terminated string
 *  VT_LPWSTR              [T][P]     wide null terminated string
 *  VT_FILETIME               [P]     FILETIME
 *  VT_BLOB                   [P]     Length prefixed bytes
 *  VT_STREAM                 [P]     Name of the stream follows
 *  VT_STORAGE                [P]     Name of the storage follows
 *  VT_STREAMED_OBJECT        [P]     Stream contains an object
 *  VT_STORED_OBJECT          [P]     Storage contains an object
 *  VT_VERSIONED_STREAM       [P]     Stream with a GUID version
 *  VT_BLOB_OBJECT            [P]     Blob contains an object
 *  VT_CF                     [P]     Clipboard format
 *  VT_CLSID                  [P]     A Class ID
 *  VT_VECTOR                 [P]     simple counted array
 *  VT_ARRAY            [V]           SAFEARRAY*
 *  VT_BYREF            [V]           void* for local use
 *  VT_BSTR_BLOB                      Reserved for system use
 */

enum VARENUM
{
    VT_EMPTY = 0,
    VT_NULL = 1,
    VT_I2 = 2,
    VT_I4 = 3,
    VT_R4 = 4,
    VT_R8 = 5,
    VT_DATE = 7,
    VT_BSTR = 8,
    VT_DISPATCH = 9,
    VT_BOOL = 11,
    VT_UNKNOWN = 13,
    VT_I1 = 16,
    VT_UI1 = 17,
    VT_UI2 = 18,
    VT_UI4 = 19,
    VT_I8 = 20,
    VT_UI8 = 21,
    VT_INT = 22,        // deprecated, don't use it!
    VT_UINT = 23,       // deprecated, don't use it!
    VT_RESERVED = 0x8000,
    VT_ILLEGAL = 0xffff,
    VT_ILLEGALMASKED = 0xfff,
    VT_TYPEMASK = 0xfff
};

typedef ULONG               PROPID;
typedef struct tagVARIANT   VARIANT;

typedef VARIANT*            LPVARIANT;
typedef VARIANT             VARIANTARG;
typedef VARIANT*            LPVARIANTARG;
typedef VARIANT*            PVARIANT;
typedef const VARIANT*      PCVARIANT;

typedef struct tagDISPPARAMS
{
    /* [size_is] */ VARIANTARG* rgvarg;
    /* [size_is] */ DISPID* rgdispidNamedArgs;
    uint32_t cArgs;
    uint32_t cNamedArgs;
} 	DISPPARAMS;

typedef struct tagEXCEPINFO {
    WORD  wCode;
    WORD  wReserved;
    BSTR  bstrSource;
    BSTR  bstrDescription;
    BSTR  bstrHelpFile;
    DWORD dwHelpContext;
    PVOID pvReserved;
    HRESULT(* pfnDeferredFillIn)(struct tagEXCEPINFO*);
    SCODE scode;
} EXCEPINFO, * LPEXCEPINFO;

typedef CY CURRENCY;

typedef struct tagSAFEARRAYBOUND
{
    ULONG cElements;
    LONG lLbound;
} 	SAFEARRAYBOUND;

typedef struct tagSAFEARRAY
{
    USHORT cDims;
    USHORT fFeatures;
    ULONG cbElements;
    ULONG cLocks;
    PVOID pvData;
    SAFEARRAYBOUND rgsabound[1];
} 	SAFEARRAY;

typedef struct tagDEC {
    USHORT wReserved;
    union {
        struct {
            BYTE scale;
            BYTE sign;
        } DUMMYSTRUCTNAME;
        USHORT signscale;
    } DUMMYUNIONNAME;
    ULONG Hi32;
    union {
        struct {
            ULONG Lo32;
            ULONG Mid32;
        } DUMMYSTRUCTNAME2;
        ULONGLONG Lo64;
    } DUMMYUNIONNAME2;
} DECIMAL;

class IUnknown
{
public:
    virtual HRESULT QueryInterface(
        /* [in] */ REFIID riid,
        /* [iid_is][out] */ void** ppvObject) = 0;

    virtual ULONG AddRef(void) = 0;
    virtual ULONG Release(void) = 0;
};

class ITypeInfo : public IUnknown
{
public:

};

class IRecordInfo : public IUnknown
{
public:

};

class IDispatch : public IUnknown
{
public:
    virtual HRESULT GetTypeInfoCount(
        /* [out] */ uint32_t* pctinfo) = 0;

    virtual HRESULT GetTypeInfo(
        /* [in] */ uint32_t iTInfo,
        /* [in] */ LCID lcid,
        /* [out] */ ITypeInfo** ppTInfo) = 0;

    virtual HRESULT GetIDsOfNames(
        /* [in] */ REFIID riid,
        /* [size_is][in] */ LPOLESTR* rgszNames,
        /* [range][in] */ uint32_t cNames,
        /* [in] */ LCID lcid,
        /* [size_is][out] */ DISPID* rgDispId) = 0;

    virtual /* [local] */ HRESULT Invoke(
        /* [annotation][in] */
          DISPID dispIdMember,
        /* [annotation][in] */
          REFIID riid,
        /* [annotation][in] */
          LCID lcid,
        /* [annotation][in] */
          WORD wFlags,
        /* [annotation][out][in] */
          DISPPARAMS* pDispParams,
        /* [annotation][out] */
          VARIANT* pVarResult,
        /* [annotation][out] */
          EXCEPINFO* pExcepInfo,
        /* [annotation][out] */
        uint32_t* puArgErr) = 0;

};

class ISequentialStream : public IUnknown
{
public:
    virtual HRESULT Read(
        /* [annotation] */
        void* pv,
        /* [annotation][in] */
        ULONG cb,
        /* [annotation] */
        ULONG * pcbRead) = 0;

    virtual HRESULT Write(
        /* [annotation] */
        const void* pv,
        /* [annotation][in] */
        ULONG cb,
        /* [annotation] */
        ULONG* pcbWritten) = 0;
};

typedef struct tagSTATSTG
{
    LPOLESTR pwcsName;
    DWORD type;
    ULARGE_INTEGER cbSize;
    FILETIME mtime;
    FILETIME ctime;
    FILETIME atime;
    DWORD grfMode;
    DWORD grfLocksSupported;
    CLSID clsid;
    DWORD grfStateBits;
    DWORD reserved;
} 	STATSTG;

typedef
enum tagSTGTY
{
    STGTY_STORAGE = 1,
    STGTY_STREAM = 2,
    STGTY_LOCKBYTES = 3,
    STGTY_PROPERTY = 4
} 	STGTY;

typedef
enum tagSTREAM_SEEK
{
    STREAM_SEEK_SET = SEEK_SET,
    STREAM_SEEK_CUR = SEEK_CUR,
    STREAM_SEEK_END = SEEK_END
} 	STREAM_SEEK;

#define CWCSTORAGENAME 32

/* Storage instantiation modes */
#define STGM_DIRECT             0x00000000L
#define STGM_TRANSACTED         0x00010000L
#define STGM_SIMPLE             0x08000000L

#define STGM_READ               0x00000000L
#define STGM_WRITE              0x00000001L
#define STGM_READWRITE          0x00000002L

#define STGM_SHARE_DENY_NONE    0x00000040L
#define STGM_SHARE_DENY_READ    0x00000030L
#define STGM_SHARE_DENY_WRITE   0x00000020L
#define STGM_SHARE_EXCLUSIVE    0x00000010L

#define STGM_PRIORITY           0x00040000L
#define STGM_DELETEONRELEASE    0x04000000L
#define STGM_NOSCRATCH          0x00100000L

#define STGM_CREATE             0x00001000L
#define STGM_CONVERT            0x00020000L
#define STGM_FAILIFTHERE        0x00000000L

#define STGM_NOSNAPSHOT         0x00200000L
#define STGM_DIRECT_SWMR        0x00400000L

typedef DWORD STGFMT;

#define STGFMT_STORAGE          0
#define STGFMT_NATIVE           1
#define STGFMT_FILE             3
#define STGFMT_ANY              4
#define STGFMT_DOCFILE          5

typedef enum tagSTATFLAG
{
    STATFLAG_DEFAULT = 0,
    STATFLAG_NONAME = 1,
    STATFLAG_NOOPEN = 2
} STATFLAG;

#define FILE_TYPE_UNKNOWN   0x0000
#define FILE_TYPE_DISK      0x0001
#define FILE_TYPE_CHAR      0x0002
#define FILE_TYPE_PIPE      0x0003
#define FILE_TYPE_REMOTE    0x8000


#define STD_INPUT_HANDLE    ((DWORD)-10)
#define STD_OUTPUT_HANDLE   ((DWORD)-11)
#define STD_ERROR_HANDLE    ((DWORD)-12)

#define NOPARITY            0
#define ODDPARITY           1
#define EVENPARITY          2
#define MARKPARITY          3
#define SPACEPARITY         4

#define ONESTOPBIT          0
#define ONE5STOPBITS        1
#define TWOSTOPBITS         2

#define IGNORE              0       // Ignore signal
#define INFINITE            0xFFFFFFFF  // Infinite timeout

//
// Baud rates at which the communication device operates
//

#define CBR_110             110
#define CBR_300             300
#define CBR_600             600
#define CBR_1200            1200
#define CBR_2400            2400
#define CBR_4800            4800
#define CBR_9600            9600
#define CBR_14400           14400
#define CBR_19200           19200
#define CBR_38400           38400
#define CBR_56000           56000
#define CBR_57600           57600
#define CBR_115200          115200
#define CBR_128000          128000
#define CBR_256000          256000

//
// Error Flags
//

#define CE_RXOVER           0x0001  // Receive Queue overflow
#define CE_OVERRUN          0x0002  // Receive Overrun Error
#define CE_RXPARITY         0x0004  // Receive Parity Error
#define CE_FRAME            0x0008  // Receive Framing error
#define CE_BREAK            0x0010  // Break Detected
#define CE_TXFULL           0x0100  // TX Queue is full
#define CE_PTO              0x0200  // LPTx Timeout
#define CE_IOE              0x0400  // LPTx I/O Error
#define CE_DNS              0x0800  // LPTx Device not selected
#define CE_OOP              0x1000  // LPTx Out-Of-Paper
#define CE_MODE             0x8000  // Requested mode unsupported

#define IE_BADID            (-1)    // Invalid or unsupported id
#define IE_OPEN             (-2)    // Device Already Open
#define IE_NOPEN            (-3)    // Device Not Open
#define IE_MEMORY           (-4)    // Unable to allocate queues
#define IE_DEFAULT          (-5)    // Error in default parameters
#define IE_HARDWARE         (-10)   // Hardware Not Present
#define IE_BYTESIZE         (-11)   // Illegal Byte Size
#define IE_BAUDRATE         (-12)   // Unsupported BaudRate

//
// Events
//

#define EV_RXCHAR           0x0001  // Any Character received
#define EV_RXFLAG           0x0002  // Received certain character
#define EV_TXEMPTY          0x0004  // Transmitt Queue Empty
#define EV_CTS              0x0008  // CTS changed state
#define EV_DSR              0x0010  // DSR changed state
#define EV_RLSD             0x0020  // RLSD changed state
#define EV_BREAK            0x0040  // BREAK received
#define EV_ERR              0x0080  // Line status error occurred
#define EV_RING             0x0100  // Ring signal detected
#define EV_PERR             0x0200  // Printer error occured
#define EV_RX80FULL         0x0400  // Receive buffer is 80 percent full
#define EV_EVENT1           0x0800  // Provider specific event 1
#define EV_EVENT2           0x1000  // Provider specific event 2

//
// Escape Functions
//

#define SETXOFF             1       // Simulate XOFF received
#define SETXON              2       // Simulate XON received
#define SETRTS              3       // Set RTS high
#define CLRRTS              4       // Set RTS low
#define SETDTR              5       // Set DTR high
#define CLRDTR              6       // Set DTR low
#define RESETDEV            7       // Reset device if possible
#define SETBREAK            8       // Set the device break line.
#define CLRBREAK            9       // Clear the device break line.

//
// PURGE function flags.
//
#define PURGE_TXABORT       0x0001  // Kill the pending/current writes to the comm port.
#define PURGE_RXABORT       0x0002  // Kill the pending/current reads to the comm port.
#define PURGE_TXCLEAR       0x0004  // Kill the transmit queue if there.
#define PURGE_RXCLEAR       0x0008  // Kill the typeahead buffer if there.

#define LPTx                0x80    // Set if ID is for LPT device

//
// Modem Status Flags
//
#define MS_CTS_ON           ((DWORD)0x0010)
#define MS_DSR_ON           ((DWORD)0x0020)
#define MS_RING_ON          ((DWORD)0x0040)
#define MS_RLSD_ON          ((DWORD)0x0080)

//
// WaitSoundState() Constants
//

#define S_QUEUEEMPTY        0
#define S_THRESHOLD         1
#define S_ALLTHRESHOLD      2

//
// Accent Modes
//

#define S_NORMAL      0
#define S_LEGATO      1
#define S_STACCATO    2

//
// SetSoundNoise() Sources
//

#define S_PERIOD512   0     // Freq = N/512 high pitch, less coarse hiss
#define S_PERIOD1024  1     // Freq = N/1024
#define S_PERIOD2048  2     // Freq = N/2048 low pitch, more coarse hiss
#define S_PERIODVOICE 3     // Source is frequency from voice channel (3)
#define S_WHITE512    4     // Freq = N/512 high pitch, less coarse hiss
#define S_WHITE1024   5     // Freq = N/1024
#define S_WHITE2048   6     // Freq = N/2048 low pitch, more coarse hiss
#define S_WHITEVOICE  7     // Source is frequency from voice channel (3)

#define S_SERDVNA     (-1)  // Device not available
#define S_SEROFM      (-2)  // Out of memory
#define S_SERMACT     (-3)  // Music active
#define S_SERQFUL     (-4)  // Queue full
#define S_SERBDNT     (-5)  // Invalid note
#define S_SERDLN      (-6)  // Invalid note length
#define S_SERDCC      (-7)  // Invalid note count
#define S_SERDTP      (-8)  // Invalid tempo
#define S_SERDVL      (-9)  // Invalid volume
#define S_SERDMD      (-10) // Invalid mode
#define S_SERDSH      (-11) // Invalid shape
#define S_SERDPT      (-12) // Invalid pitch
#define S_SERDFQ      (-13) // Invalid frequency
#define S_SERDDR      (-14) // Invalid duration
#define S_SERDSR      (-15) // Invalid source
#define S_SERDST      (-16) // Invalid state

#define NMPWAIT_WAIT_FOREVER            0xffffffff
#define NMPWAIT_NOWAIT                  0x00000001
#define NMPWAIT_USE_DEFAULT_WAIT        0x00000000

#define OF_READ             0x00000000
#define OF_WRITE            0x00000001
#define OF_READWRITE        0x00000002
#define OF_SHARE_COMPAT     0x00000000
#define OF_SHARE_EXCLUSIVE  0x00000010
#define OF_SHARE_DENY_WRITE 0x00000020
#define OF_SHARE_DENY_READ  0x00000030
#define OF_SHARE_DENY_NONE  0x00000040
#define OF_PARSE            0x00000100
#define OF_DELETE           0x00000200
#define OF_VERIFY           0x00000400
#define OF_CANCEL           0x00000800
#define OF_CREATE           0x00001000
#define OF_PROMPT           0x00002000
#define OF_EXIST            0x00004000
#define OF_REOPEN           0x00008000

class IStream : public ISequentialStream
{
public:
    virtual HRESULT Seek(
        /* [in] */ LARGE_INTEGER dlibMove,
        /* [in] */ DWORD dwOrigin,
        /* [annotation] */
        ULARGE_INTEGER* plibNewPosition) = 0;

    virtual HRESULT SetSize(
        /* [in] */ ULARGE_INTEGER libNewSize) = 0;

    virtual /* [local] */ HRESULT CopyTo(
        /* [annotation][unique][in] */
         IStream* pstm,
        /* [in] */ ULARGE_INTEGER cb,
        /* [annotation] */
        ULARGE_INTEGER* pcbRead,
        /* [annotation] */
        ULARGE_INTEGER* pcbWritten) = 0;

    virtual HRESULT Commit(
        /* [in] */ DWORD grfCommitFlags) = 0;

    virtual HRESULT Revert(void) = 0;

    virtual HRESULT LockRegion(
        /* [in] */ ULARGE_INTEGER libOffset,
        /* [in] */ ULARGE_INTEGER cb,
        /* [in] */ DWORD dwLockType) = 0;

    virtual HRESULT UnlockRegion(
        /* [in] */ ULARGE_INTEGER libOffset,
        /* [in] */ ULARGE_INTEGER cb,
        /* [in] */ DWORD dwLockType) = 0;

    virtual HRESULT Stat(
        /* [out] */ STATSTG* pstatstg,
        /* [in] */ DWORD grfStatFlag) = 0;

    virtual HRESULT Clone(
        /* [out] */ IStream** ppstm) = 0;

};

typedef IStream* LPSTREAM;

class IPersist : public IUnknown
{
public:
    virtual HRESULT GetClassID(
        /* [out] */ CLSID* pClassID) = 0;

};

class IPersistStream : public IPersist
{
public:
    virtual HRESULT IsDirty(void) = 0;

    virtual HRESULT Load(
        /* [unique][in] */ IStream* pStm) = 0;

    virtual HRESULT Save(
        /* [unique][in] */ IStream* pStm,
        /* [in] */ BOOL fClearDirty) = 0;

    virtual HRESULT GetSizeMax(
        /* [out] */ ULARGE_INTEGER* pcbSize) = 0;

};

class IPersistStreamInit : public IPersist
{
public:
    virtual HRESULT IsDirty(void) = 0;

    virtual HRESULT Load(
        /* [in] */ LPSTREAM pStm) = 0;

    virtual HRESULT Save(
        /* [in] */ LPSTREAM pStm,
        /* [in] */ BOOL fClearDirty) = 0;

    virtual HRESULT GetSizeMax(
        /* [out] */ ULARGE_INTEGER* pCbSize) = 0;

    virtual HRESULT InitNew(void) = 0;
};

extern const IID IID_IUnknown;
extern const IID IID_IPersistStreamInit;
extern const IID IID_IPersistStream;
extern const IID IID_IStream;
extern const IID IID_ISequentialStream;

struct tagVARIANT
{
    VARTYPE vt;
    union
    {
        LONGLONG        llVal;
        LONG            lVal;
        BYTE            bVal;
        SHORT           iVal;
        FLOAT           fltVal;
        DOUBLE          dblVal;
        VARIANT_BOOL    boolVal;
        DATE            date;
        BSTR            bstrVal;
        IUnknown*       punkVal;
        IDispatch*      pdispVal;
        CHAR            cVal;
        USHORT          uiVal;
        ULONG           ulVal;
        ULONGLONG       ullVal;
    };
};

bool CreateProcess(const char* cmd, FILE** readPipe, FILE** writePipe = nullptr);

BSTR SysAllocString(const OLECHAR* psz);
BSTR SysAllocStringLen(const OLECHAR* psz, size_t len);

inline void SysFreeString(BSTR bstrString)
{
    if (bstrString)
    {
        ::free((void*)bstrString);
    }
}

inline void VariantInit(PVARIANT var)
{
    if (var)
    {
        memset(var, 0, sizeof(VARIANT));
    }
}

HRESULT VariantClear(PVARIANT var);
HRESULT VariantCopyInd(VARIANT* pvarDest, const VARIANTARG* pvargSrc);
HRESULT VariantChangeType(VARIANTARG* pvargDest, const VARIANTARG* pvarSrc, USHORT wFlags, VARTYPE vt);
HRESULT CreateStreamOnHGlobal(HGLOBAL hGlobal, BOOL fDeleteOnRelease, LPSTREAM* ppstm);

inline HRESULT VariantCopy(VARIANTARG* pvargDest, const VARIANTARG* pvargSrc)
{
    return VariantCopyInd(pvargDest, pvargSrc);
}

#endif // _WIN32

template <class T>
class SharedPtr
{
public:
    typedef SharedPtr<T> thisClass;
    typedef T            value_type;

    SharedPtr() noexcept
        : p(nullptr)
    {
    }

    SharedPtr(T* _p) noexcept
        : p(_p)
    {
        if (p)
        {
            p->AddRef();
        }
    }

    SharedPtr(const SharedPtr& i) noexcept
        : p((T*)i.p)
    {
        if (p)
        {
            p->AddRef();
        }
    }

    SharedPtr(SharedPtr&& i) noexcept
    {
        p = i.Detach();
    }

    template<class I>
    SharedPtr(SharedPtr<I>&& i) noexcept
    {
        p = (T*)i.Detach();
    }

    template<class I>
    SharedPtr(const SharedPtr<I>& i) noexcept
        : p((T*)i.p)
    {
        if (p)
        {
            p->AddRef();
        }
    }

    ~SharedPtr() noexcept
    {
        Clear();
    }

public:
    inline bool IsValid() const noexcept
    {
        return p ? true : false;
    }

    inline bool IsUnique() const noexcept
    {
        if (p)
        {
            return p->IsUnique() ? true : false;
        }
        return true;
    }

    inline void Attach(T* _p) noexcept
    {
        if (p)
        {
            p->Release();
        }
        p = _p;
    }

    inline T* Detach(T* _p = nullptr) noexcept
    {
        T* pDetach = p;
        p = _p;
        return pDetach;
    }

    inline int32_t Clear() noexcept
    {
        int32_t result = 0;

        if (p)
        {
            result = p->Release();
            p = nullptr;
        }
        return result;
    }

    inline void Assign(T* _p) noexcept
    {
        if (_p)
        {
            _p->AddRef();
        }
        Attach(_p);
    }

    inline T* operator->() const noexcept
    {
        assert(p);
        return p;
    }

    inline operator bool() const noexcept
    {
        return !!p;
    }

    inline operator T* () const noexcept
    {
        return p;
    }

    inline T& operator*() const noexcept
    {
        assert(p);
        return *p;
    }

    inline SharedPtr& operator=(const T* _p) noexcept
    {
        Assign(_p);
        return *this;
    }

    inline SharedPtr& operator=(const SharedPtr& i) noexcept
    {
        Assign(i.p);
        return *this;
    }

    inline SharedPtr& operator=(thisClass&& i) noexcept
    {
        if (p)
        {
            p->Release();
        }
        p = i.p;
        i.p = nullptr;

        return *this;
    }

    template<class I>
    inline SharedPtr& operator=(SharedPtr<I>&& i) noexcept
    {
        if (p)
        {
            p->Release();
        }
        p = (T*)i.p;
        i.p = nullptr;

        return *this;
    }

    template<class I>
    inline SharedPtr& operator=(const SharedPtr<I>& i) noexcept
    {
        Assign((T*)i.p);
        return *this;
    }

    inline SharedPtr& operator=(T* _p) noexcept
    {
        Assign(_p);
        return *this;
    }

    inline bool operator==(const SharedPtr& i) const noexcept
    {
        return p == i.p ? true : false;
    }

    inline bool operator!=(const SharedPtr& i) const noexcept
    {
        return p != i.p ? true : false;
    }

    inline bool operator<(const SharedPtr& i) const noexcept
    {
        return p < i.p ? true : false;
    }

public:
    T* p;
};

template<class T, class... Arguments>
inline SharedPtr<T> MakeShared(Arguments&&... args)
{
    return new T(std::forward<Arguments>(args)...);
}

template <class T = IUnknown>
class RefCount : public T
{
public:
    RefCount() noexcept
        : refCount{ 0 }
    {
    }

    virtual ~RefCount()
    {
    }

    RefCount(const RefCount&) = delete;
    RefCount& operator=(const RefCount&) = delete;

public:
    ULONG STDMETHODCALLTYPE AddRef(void) override
    {
        return (ULONG) ++refCount;
    }
    ULONG STDMETHODCALLTYPE Release(void) override
    {
        const int32_t n = --refCount;

        assert(n >= 0);

        if (n <= 0)
        {
            delete this;
        }
        return (ULONG)n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(const IID& iid, void** ppv) override
    {
        assert(ppv);
        *ppv = nullptr;

        if (::InlineIsEqualGUID(iid, IID_IUnknown))
        {
            *ppv = (IUnknown*)(T*)this;
        }
        if (*ppv)
        {
            ((IUnknown*)*ppv)->AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    int32_t TestRef() const
    {
        return refCount;
    }
    bool IsUnique() const
    {
        return refCount == 1 ? true : false;
    }

protected:
    std::atomic<int32_t> refCount;
};

class BlobStream : public RefCount<IStream>
{
public:
    enum class Mode
    {
        blob,
        pipeOpen,
        pipeClosed
    };

    BlobStream(std::vector<uint8_t>&& buf) noexcept
        : m_buffer(std::move(buf))
        , m_readPos(0)
        , m_writePos(0)
        , m_bWriteStream(false)
        , m_mode(Mode::blob)
    {
    }

    BlobStream(size_t n = 0, const void* p = nullptr)
        : m_mode(Mode::blob)
    {
        InitFromMemory(n, p);
    }

    BlobStream(const std::vector<uint8_t>& buf)
        : m_mode(Mode::blob)
    {
        InitFromMemory(buf.size(), buf.data());
    }

    template<class T>
    BlobStream(const std::basic_string<T>& str)
        : m_mode(Mode::blob)
    {
        operator=(str);
    }

    BlobStream(const BlobStream&) = delete;
    BlobStream& operator=(const BlobStream&) = delete;

    ~BlobStream()
    {
    }

    void SetMode(const Mode m) noexcept
    {
        const std::lock_guard<std::mutex> guard(m_mtxData);
        m_mode = m;

        if (m == Mode::pipeClosed)
        {
            m_hasData.notify_all();
        }
    }

    void Clear() noexcept
    {
        const std::lock_guard<std::mutex> guard(m_mtxData);

        m_buffer.clear();
        m_readPos = 0;
        m_writePos = 0;
        m_bWriteStream = false;
    }

    size_t GetSize() const noexcept
    {
        const std::lock_guard<std::mutex> guard(m_mtxData);
        return m_buffer.size();
    }

    static size_t GetSize(IStream* pStream) noexcept
    {
        assert(pStream);
        size_t n = 0;

        LARGE_INTEGER cur;

        if (SUCCEEDED(pStream->Seek({}, STREAM_SEEK_CUR, (ULARGE_INTEGER*)&cur)))
        {
            ULARGE_INTEGER result;

            if (SUCCEEDED(pStream->Seek({}, STREAM_SEEK_END, &result)))
            {
                n = (size_t)result.QuadPart;
                pStream->Seek(cur, STREAM_SEEK_SET, nullptr);
            }
        }
        return n;
    }

    static size_t Tell(IStream* pStream) noexcept
    {
        size_t n = 0;

        ULARGE_INTEGER cur;

        if (SUCCEEDED(pStream->Seek({}, STREAM_SEEK_CUR, &cur)))
        {
            n = (size_t)cur.QuadPart;
        }
        return n;
    }

    inline bool FromFile(const std::string& path) noexcept
    {
        Clear();

        const std::lock_guard<std::mutex> guard(m_mtxData);
        FILE* h = nullptr;

#ifdef _WIN32
        fopen_s(&h, path.c_str(), "r+b");
#else
        h = fopen64(path.c_str(), "r+b");
#endif
        if (h)
        {
#ifdef _WIN32
            fseek(h, 0, SEEK_END);
            const size_t size = static_cast<size_t>(ftell(h));
#else
            fseeko64(h, 0, SEEK_END);
            const size_t size = static_cast<size_t>(ftello64(h));
#endif
            bool result = true;

            if (size)
            {
                try
                {
                    m_buffer.resize(size);
                }
                catch (...)
                {
                    fclose(h);
                    return false;
                }
#ifdef _WIN32
                fseek(h, 0, SEEK_SET);
#else
                fseeko64(h, 0, SEEK_SET);
#endif
                result = fread(m_buffer.data(), 1, size, h) == size;
            }
            fclose(h);
            return result;
        }
        return false;
    }

    inline bool ToFile(const std::string& path, bool append = false) const noexcept
    {
        const std::lock_guard<std::mutex> guard(m_mtxData);

        FILE* h = nullptr;

#ifdef _WIN32
        fopen_s(&h, path.c_str(), append ? "a+b" : "r+b");
#else
        h = fopen64(path.c_str(), append ? "a+b" : "r+b");
#endif
        if (!h)
        {
#ifdef _WIN32
            fopen_s(&h, path.c_str(), append ? "ab" : "wb");
#else
            h = fopen64(path.c_str(), append ? "ab" : "wb");
#endif
        }
        if (h)
        {
            const bool result = fwrite(m_buffer.data(), 1, m_buffer.size(), h) == m_buffer.size();

            fclose(h);
            return result;
        }
        return false;
    }

    template<class T>
    BlobStream& operator=(const std::basic_string<T>& str)
    {
        InitFromMemory(str.length() * sizeof(T), str.c_str());
        return *this;
    }

    void InitFromMemory(size_t nSize, const void* p = nullptr)
    {
        Clear();

        if (nSize)
        {
            const std::lock_guard<std::mutex> guard(m_mtxData);

            m_buffer.resize(nSize);

            if (p)
            {
                memcpy(m_buffer.data(), p, nSize);
            }
        }
    }

    std::vector<uint8_t> GetBuffer() const
    {
        const std::lock_guard<std::mutex> guard(m_mtxData);
        return m_buffer;
    }

    static HRESULT CopyTo(
        IStream* pstmFrom, IStream* pstmTo, ULARGE_INTEGER cb, ULARGE_INTEGER* pcbRead, ULARGE_INTEGER* pcbWritten) noexcept
    {
        if (pstmFrom == nullptr || pstmTo == nullptr)
        {
            return E_INVALIDARG;
        }
        ULONG n = cb.u.HighPart == 0 && cb.u.LowPart < 0x100000 ? cb.u.LowPart : 0x100000;
        std::vector<BYTE> buf;

        try
        {
            buf.resize(n);
        }
        catch (...)
        {
            return E_OUTOFMEMORY;
        }

        ULONG dwRead = 0;
        ULONG dwWritten = 0;

        if (pcbRead != nullptr)
        {
            pcbRead->QuadPart = 0;
        }
        if (pcbWritten != nullptr)
        {
            pcbWritten->QuadPart = 0;
        }

        HRESULT hr = S_OK;

        while (cb.QuadPart > 0)
        {
            hr = pstmFrom->Read(buf.data(), n, &dwRead);

            if (FAILED(hr) || dwRead == 0)
            {
                break;
            }
            if (pcbRead)
            {
                pcbRead->QuadPart += dwRead;
            }
            hr = pstmTo->Write(buf.data(), dwRead, &dwWritten);

            if (FAILED(hr))
            {
                break;
            }
            if (pcbWritten)
            {
                pcbWritten->QuadPart += dwWritten;
            }
            cb.QuadPart -= dwRead;

            if (n > cb.QuadPart)
            {
                n = cb.u.LowPart;
            }
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(const IID& iid, void** ppv) override
    {
        assert(ppv);
        *ppv = nullptr;

        if (::InlineIsEqualGUID(iid, IID_IStream))
        {
            *ppv = this;
        }
        if (*ppv)
        {
            ((IUnknown*)*ppv)->AddRef();
            return S_OK;
        }
        return RefCount<IStream>::QueryInterface(iid, ppv);
    }

    HRESULT STDMETHODCALLTYPE CopyTo
    (
        /* [unique][in] */ IStream* pstm,
        /* [in] */ ULARGE_INTEGER cb,
        /* [out] */ ULARGE_INTEGER* pcbRead,
        /* [out] */ ULARGE_INTEGER* pcbWritten) override
    {
        return CopyTo(this, pstm, cb, pcbRead, pcbWritten);
    }

    HRESULT STDMETHODCALLTYPE Commit
    (
        /* [in] */ DWORD grfCommitFlags) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE Revert(void) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE LockRegion
    (
        /* [in] */ ULARGE_INTEGER libOffset,
        /* [in] */ ULARGE_INTEGER cb,
        /* [in] */ DWORD          dwLockType) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE UnlockRegion
    (
        /* [in] */ ULARGE_INTEGER libOffset,
        /* [in] */ ULARGE_INTEGER cb,
        /* [in] */ DWORD          dwLockType) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE Stat
    (
        /* [out] */ STATSTG* pstatstg,
        /* [in] */ DWORD) override
    {
        if (pstatstg != nullptr)
        {
            const std::lock_guard<std::mutex> guard(m_mtxData);

            memset(pstatstg, 0, sizeof(STATSTG));

            pstatstg->type = STGTY_STREAM;
            pstatstg->cbSize.QuadPart = m_buffer.size();
            pstatstg->grfMode = STGM_READWRITE;

            return S_OK;
        }
        return E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE Clone
    (
        /* [out] */ IStream** ppstm) override
    {
        if (ppstm == nullptr || *ppstm)
        {
            return E_INVALIDARG;
        }
        try
        {
            *ppstm = new BlobStream;
        }
        catch (...)
        {
            *ppstm = nullptr;
        }
        if ((*ppstm) == nullptr)
        {
            return E_OUTOFMEMORY;
        }

        (*ppstm)->AddRef();

        const auto readPos = m_readPos;
        m_readPos = 0;
        ULARGE_INTEGER cb;
        cb.QuadPart = static_cast<ULONGLONG>(m_buffer.size());

        const HRESULT hr = CopyTo(*ppstm, cb, nullptr, nullptr);

        m_readPos = readPos;

        if (FAILED(hr))
        {
            (*ppstm)->Release();
            *ppstm = nullptr;
            return hr;
        }
        return (*ppstm)->Seek({}, SEEK_SET, nullptr);
    }

    HRESULT STDMETHODCALLTYPE Read(
        /* [out] */ void* pv,
        /* [in] */ ULONG cb,
        /* [out] */ ULONG* pcbRead) override
    {
        m_bWriteStream = false;

        if (pcbRead)
        {
            *pcbRead = 0;
        }
        if (0 == cb)
        {
            return S_OK;
        }
        if (!pv)
        {
            return STG_E_INVALIDPOINTER;
        }
        std::unique_lock<std::mutex> sync(m_mtxData);

        if (m_readPos > static_cast<ULONG>(m_buffer.size()))
        {
            assert(m_mode == Mode::blob);
            return STG_E_SEEKERROR;
        }
        ULONG cBytesLeft = static_cast<ULONG>(m_buffer.size()) - m_readPos;

        if (0 == cBytesLeft)
        {
            switch (m_mode)
            {
            case Mode::pipeClosed:
                return STG_E_NOMOREFILES;

            case Mode::pipeOpen:
            {
                assert(m_readPos == 0);

                do
                {
                    m_hasData.wait(sync);

                    cBytesLeft = static_cast<ULONG>(m_buffer.size()) - m_readPos;

                    if (0 == cBytesLeft && m_mode == Mode::pipeClosed)
                    {
                        return STG_E_NOMOREFILES;
                    }
                } while(cBytesLeft == 0);
            }   
            break;

            default:
                return S_FALSE;
            }
        }
        const ULONG cBytesRead = cb > cBytesLeft ? cBytesLeft : cb;
        memcpy(pv, (const void*)(m_buffer.data() + m_readPos), cBytesRead);

        if (m_mode == Mode::blob)
        {
            m_readPos += cBytesRead;
        }
        else
        {
            const size_t newSize = m_buffer.size() - cBytesRead;

            if (newSize)
            {
                memmove(m_buffer.data(), m_buffer.data()+cBytesRead, newSize);
            }
            m_buffer.resize(newSize);
        }
        if (pcbRead)
        {
            *pcbRead = cBytesRead;
        }
        if (cb != cBytesRead && m_mode == Mode::blob)
        { 
            return S_FALSE;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Write(
        /* [in] */ const void* pv,
        /* [in] */ ULONG cb,
        /* [out] */ ULONG* pcbWritten) override
    {
        m_bWriteStream = true;

        if (pcbWritten)
        {
            *pcbWritten = 0;
        }
        if (0 == cb)
        {
            return S_OK;
        }
        if (!pv)
        {
            return STG_E_INVALIDPOINTER;
        }
        const std::lock_guard<std::mutex> guard(m_mtxData);

        if (m_mode != Mode::blob)
        {
            if (m_mode == Mode::pipeClosed)
            {
                return STG_E_WRITEFAULT;
            }
            m_writePos = static_cast<ULONG>(m_buffer.size());
        }
        if ((m_writePos + cb) > static_cast<ULONG>(m_buffer.size()))
        {
            try
            {
                m_buffer.resize(m_writePos + cb);
            }
            catch (...)
            {
                return E_OUTOFMEMORY;
            }
        }

        memcpy((void*)(m_buffer.data() + m_writePos), pv, cb);

        if (pcbWritten)
        {
            *pcbWritten = cb;
        }
        if (m_mode == Mode::blob)
        {
            m_writePos += cb;
        }
        else
        {
            m_hasData.notify_one();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Seek(
        /* [in] */ LARGE_INTEGER   dlibMove,
        /* [in] */ DWORD           dwOrigin,
        /* [out] */ ULARGE_INTEGER* plibNewPosition) override
    {
        LONG lMove = dlibMove.u.LowPart;

        const std::lock_guard<std::mutex> guard(m_mtxData);

        if (m_mode == Mode::blob)
        {       
            switch (dwOrigin)
            {
            case STREAM_SEEK_SET:
            {
                m_readPos = lMove;
                m_writePos = lMove;
            }
            break;

            case STREAM_SEEK_CUR:
            {
                m_readPos += lMove;
                m_writePos += lMove;
            }
            break;

            case STREAM_SEEK_END:
            {
                m_readPos = static_cast<ULONG>(m_buffer.size()) + lMove;
                m_writePos = static_cast<ULONG>(m_buffer.size()) + lMove;
            }
            break;
            }
            assert(m_readPos <= m_buffer.size());

            if (plibNewPosition != nullptr)
            {
                memset(plibNewPosition, 0, sizeof(ULARGE_INTEGER));
                plibNewPosition->u.LowPart = m_bWriteStream ? m_writePos : m_readPos;
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSize(
        /* [in] */ ULARGE_INTEGER libNewSize) override
    {
        const std::lock_guard<std::mutex> guard(m_mtxData);

        if (m_mode != Mode::blob)
        {
            return STG_E_WRITEFAULT;
        }            
        assert(libNewSize.u.HighPart == 0);

        if (libNewSize.u.LowPart == 0)
        {
            Clear();
            return S_OK;
        }

        try
        {
            m_buffer.resize(libNewSize.u.LowPart);
        }
        catch (...)
        {
            return E_OUTOFMEMORY;
        }

        if (m_readPos > static_cast<ULONG>(m_buffer.size()))
        {
            m_readPos = static_cast<ULONG>(m_buffer.size());
        }
        if (m_writePos > static_cast<ULONG>(m_buffer.size()))
        {
            m_writePos = static_cast<ULONG>(m_buffer.size());
        }
        return S_OK;
    }

private:
    std::vector<BYTE> m_buffer;
    ULONG m_readPos;
    ULONG m_writePos;
    std::atomic_bool  m_bWriteStream;
    std::condition_variable m_hasData;
    mutable std::mutex m_mtxData;
    Mode m_mode;
};

template <class T>
inline LARGE_INTEGER ToLargeInteger(const T value)
{
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)value;
    return li;
}

template <class T>
inline ULARGE_INTEGER ToULargeInteger(const T value)
{
    ULARGE_INTEGER uli;
    uli.QuadPart = (ULONGLONG)value;
    return uli;
}

class Variant : public tagVARIANT
{
    // Constructors
public:
    Variant() noexcept
    {
        ::VariantInit(this);
    }
    ~Variant()
    {
        Clear();
    }
    Variant(const VARIANT& varSrc)
    {
        vt = VT_EMPTY;
        Copy(&varSrc);
    }
    Variant(const Variant& varSrc)
    {
        vt = VT_EMPTY;
        Copy(&varSrc);
    }
    Variant(Variant&& varSrc) noexcept
    {
        memcpy(this, &varSrc, sizeof(Variant));
        ::VariantInit(&varSrc);
    }
    Variant(LPCOLESTR lpszSrc)
    {
        vt = VT_EMPTY;
        *this = lpszSrc;
    }
    Variant(const std::wstring& lpszSrc)
    {
        vt = VT_EMPTY;
        *this = lpszSrc;
    }
    Variant(const std::wstring_view& lpszSrc)
    {
        vt = VT_EMPTY;
        *this = lpszSrc;
    }
    Variant(const std::string_view& lpszSrc)
    {
        vt = VT_EMPTY;
        *this = lpszSrc;
    }
    Variant(PCSTR lpszSrc)
    {
        vt = VT_EMPTY;
        *this = lpszSrc;
    }
    Variant(const std::string& lpszSrc)
    {
        vt = VT_EMPTY;
        *this = lpszSrc;
    }
    Variant(bool bSrc)
    {
        vt = VT_BOOL;
        boolVal = bSrc ? VARIANT_TRUE : VARIANT_FALSE;
    }
    Variant(BYTE nSrc)
    {
        vt = VT_UI1;
        bVal = nSrc;
    }
    Variant(int16_t nSrc)
    {
        vt = VT_I2;
        iVal = nSrc;
    }
    Variant(int32_t nSrc)
    {
        vt = VT_I4;
        lVal = nSrc;
    }
    Variant(float fltSrc)
    {
        vt = VT_R4;
        fltVal = fltSrc;
    }
    Variant(double dblSrc)
    {
        vt = VT_R8;
        dblVal = dblSrc;
    }
    Variant(LONGLONG nSrc)
    {
        vt = VT_I8;
        llVal = nSrc;
    }
    Variant(ULONGLONG nSrc)
    {
        vt = VT_UI8;
        ullVal = nSrc;
    }
    Variant(IDispatch* pSrc)
    {
        vt = VT_DISPATCH;
        pdispVal = pSrc;

        if (pdispVal)
        {
            pdispVal->AddRef();
        }
    }
    Variant(IUnknown* pSrc)
    {
        vt = VT_UNKNOWN;
        punkVal = pSrc;

        if (punkVal)
        {
            punkVal->AddRef();
        }
    }
    Variant(char cSrc)
    {
        vt = VT_I1;
        cVal = cSrc;
    }
    Variant(uint16_t nSrc)
    {
        vt = VT_UI2;
        uiVal = nSrc;
    }
    Variant(uint32_t nSrc)
    {
        vt = VT_UI4;
        ulVal = nSrc;
    }
    Variant(const std::vector<uint8_t>& src)
    {
        vt = VT_EMPTY;
        punkVal = nullptr;
        operator=(src);
    }
    Variant(std::vector<uint8_t>&& src)
    {
        vt = VT_EMPTY;
        punkVal = nullptr;
        operator=(std::move(src));
    }
    template<class T>
    Variant(const SharedPtr<T>& src)
    {
        vt = VT_EMPTY;
        punkVal = nullptr;
        operator=(src);
    }
    template<class T>
    Variant(SharedPtr<T>&& src)
    {
        vt = VT_EMPTY;
        punkVal = nullptr;
        operator=(std::move(src));
    }
#ifdef _WIN32
    Variant(long nSrc)
    {
        vt = VT_I4;
        lVal = nSrc;
    }
#endif
    template<class T>
    Variant(T nSrc, VARTYPE vtSrc)
    {
        switch (vtSrc)
        {
        case VT_INT:
        case VT_I4:
            lVal = (LONG)nSrc;
            break;
        case VT_UINT:
        case VT_UI4:
            ulVal = (ULONG)nSrc;
            break;
        case VT_I1:
            cVal = (CHAR)nSrc;
            break;
        case VT_UI1:
            bVal = (BYTE)nSrc;
            break;
        case VT_I2:
            iVal = (SHORT)nSrc;
            break;
        case VT_UI2:
            uiVal = (USHORT)nSrc;
            break;
        case VT_I8:
            llVal = (LONGLONG)nSrc;
            break;
        case VT_UI8:
            ullVal = (ULONGLONG)nSrc;
            break;
        case VT_EMPTY:
        case VT_NULL:
            break;
        case VT_BOOL:
            boolVal = nSrc ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        case VT_R4:
            fltVal = (FLOAT)nSrc;
            break;
        case VT_R8:
        case VT_DATE:
            dblVal = (DOUBLE)nSrc;
            break;
        }
        vt = vtSrc;
    }

    // Assignment Operators
public:
    Variant& operator=(const Variant& varSrc)
    {
        if (this != &varSrc)
        {
            Copy(&varSrc);
        }
        return *this;
    }
    Variant& operator=(Variant&& varSrc) noexcept
    {
        memcpy(this, &varSrc, sizeof(Variant));
        ::VariantInit(&varSrc);
        return *this;
    }
    Variant& operator=(const VARIANT& varSrc)
    {
        if (static_cast<VARIANT*>(this) != &varSrc)
        {
            Copy(&varSrc);
        }
        return *this;
    }
    Variant& operator=(VARIANT&& varSrc)
    {
        memcpy(this, &varSrc, sizeof(Variant));
        ::VariantInit(&varSrc);
        return *this;
    }    
    Variant& operator=(LPCOLESTR lpszSrc)
    {
        if (vt != VT_BSTR || bstrVal != lpszSrc)
        {
            Clear();
            bstrVal = SysAllocString(lpszSrc);
            vt = VT_BSTR;
        }
        return *this;
    }
    Variant& operator=(const std::wstring& lpszSrc)
    {
        Clear();

#ifdef _WIN32
        bstrVal = SysAllocStringLen(lpszSrc.c_str(), static_cast<UINT>(lpszSrc.length()));
#else
        bstrVal = SysAllocStringLen(lpszSrc.c_str(), lpszSrc.length());
#endif

        vt = VT_BSTR;
        return *this;
    }
    Variant& operator=(const std::wstring_view& lpszSrc)
    {
        Clear();

#ifdef _WIN32
        bstrVal = SysAllocStringLen(lpszSrc.data(), static_cast<UINT>(lpszSrc.length()));
#else
        bstrVal = SysAllocStringLen(lpszSrc.data(), lpszSrc.length());
#endif

        vt = VT_BSTR;
        return *this;
    }
    Variant& operator=(PCSTR lpszSrc)
    {
        return operator=(CA2WEX(lpszSrc));
    }
    Variant& operator=(const std::string& lpszSrc)
    {
        Clear();
        bstrVal = SysAllocString(CA2WEX(lpszSrc).c_str());
        vt = VT_BSTR;
        return *this;
    }
    Variant& operator=(const std::string_view& lpszSrc)
    {
        operator=(std::string(lpszSrc.data(), lpszSrc.length()));
        return *this;
    }
    Variant& operator=(BYTE nSrc)
    {
        if (vt != VT_UI1)
        {
            Clear();
            vt = VT_UI1;
        }
        bVal = nSrc;
        return *this;
    }
    Variant& operator=(int16_t nSrc)
    {
        if (vt != VT_I2)
        {
            Clear();
            vt = VT_I2;
        }
        iVal = nSrc;
        return *this;
    }
    Variant& operator=(int32_t nSrc)
    {
        if (vt != VT_I4)
        {
            Clear();
            vt = VT_I4;
        }
        lVal = nSrc;
        return *this;
    }
    Variant& operator=(float fltSrc)
    {
        if (vt != VT_R4)
        {
            Clear();
            vt = VT_R4;
        }
        fltVal = fltSrc;
        return *this;
    }
    Variant& operator=(double dblSrc)
    {
        if (vt != VT_R8)
        {
            Clear();
            vt = VT_R8;
        }
        dblVal = dblSrc;
        return *this;
    }
    Variant& operator=(LONGLONG nSrc)
    {
        if (vt != VT_I8)
        {
            Clear();
            vt = VT_I8;
        }
        llVal = nSrc;
        return *this;
    }
    Variant& operator=(ULONGLONG nSrc)
    {
        if (vt != VT_UI8)
        {
            Clear();
            vt = VT_UI8;
        }
        ullVal = nSrc;
        return *this;
    }
    Variant& operator=(IDispatch* pSrc)
    {
        if (vt != VT_DISPATCH || pSrc != pdispVal)
        {
            Clear();
            vt = VT_DISPATCH;
            pdispVal = pSrc;

            if (pdispVal)
            {
                pdispVal->AddRef();
            }
        }
        return *this;
    }
    Variant& operator=(IUnknown* pSrc)
    {
        if (vt != VT_DISPATCH || pSrc != punkVal)
        {
            Clear();
            vt = VT_UNKNOWN;
            punkVal = pSrc;

            if (punkVal)
            {
                punkVal->AddRef();
            }
        }
        return *this;
    }
    Variant& operator=(const std::vector<uint8_t>& src);
    Variant& operator=(std::vector<uint8_t>&& src);

    template<class T>
    Variant& operator=(const SharedPtr<T>& src)
    {
        Clear();
        punkVal = src.p;

        if (punkVal)
        {
            punkVal->AddRef();
        }
        vt = VT_UNKNOWN;
        return *this;
    }

    template<class T>
    Variant& operator=(SharedPtr<T>&& src)
    {
        Clear();
        punkVal = src.Detach();
        vt = VT_UNKNOWN;
        return *this;
    }

    Variant& operator=(char cSrc)
    {
        if (vt != VT_I1)
        {
            Clear();
            vt = VT_I1;
        }
        cVal = cSrc;
        return *this;
    }
    Variant& operator=(uint16_t nSrc)
    {
        if (vt != VT_UI2)
        {
            Clear();
            vt = VT_UI2;
        }
        uiVal = nSrc;
        return *this;
    }
    Variant& operator=(uint32_t nSrc)
    {
        if (vt != VT_UI4)
        {
            Clear();
            vt = VT_UI4;
        }
        ulVal = nSrc;
        return *this;
    }
#ifdef _WIN32
    Variant& operator=(long nSrc)
    {
        if (vt != VT_I4)
        {
            Clear();
            vt = VT_I4;
        }
        lVal = nSrc;
        return *this;
    }
#endif

    // Comparison Operators
public:
    inline bool operator==(const VARIANT& varSrc) const noexcept
    {
        return VarCmp(this, &varSrc, LOCALE_USER_DEFAULT, 0) == static_cast<HRESULT>(VARCMP_EQ);
    }

    inline bool operator!=(const VARIANT& varSrc) const noexcept
    {
        return !operator==(varSrc);
    }

    inline bool operator<(const VARIANT& varSrc) const noexcept
    {
        return VarCmp(this, &varSrc, LOCALE_USER_DEFAULT, 0) == static_cast<HRESULT>(VARCMP_LT);
    }

    inline bool operator>(const VARIANT& varSrc) const noexcept
    {
        return VarCmp(this, &varSrc, LOCALE_USER_DEFAULT, 0) == static_cast<HRESULT>(VARCMP_GT);
    }

    inline bool operator<=(const VARIANT& varSrc) const noexcept
    {
        const auto cmp = VarCmp(this, &varSrc, LOCALE_USER_DEFAULT, 0);
        return cmp == static_cast<HRESULT>(VARCMP_LT) || cmp == static_cast<HRESULT>(VARCMP_EQ);
    }

    inline bool operator>=(const VARIANT& varSrc) const noexcept
    {
        const auto cmp = VarCmp(this, &varSrc, LOCALE_USER_DEFAULT, 0);
        return cmp == static_cast<HRESULT>(VARCMP_GT) || cmp == static_cast<HRESULT>(VARCMP_EQ);
    }

public:
    static HRESULT  VarCmp(PCVARIANT pvarLeft, PCVARIANT pvarRight, LCID lcid, ULONG dwFlags) noexcept;
    static int      VarTypePriority(VARTYPE vt) noexcept;

    // Operations
public:
    inline HRESULT Clear() noexcept
    {
        return ::VariantClear(this);
    }
    inline HRESULT Copy(const VARIANT* pSrc) noexcept
    {
        return ::VariantCopy(this, const_cast<VARIANT*>(pSrc));
    }
    inline HRESULT CopyFrom(const VARIANT* pSrc) noexcept
    {
        return ::VariantCopy(this, const_cast<VARIANT*>(pSrc));
    }
    inline HRESULT CopyTo(VARIANT* pDest) noexcept
    {
        return ::VariantCopy(pDest, this);
    }
    inline HRESULT ChangeType(VARTYPE vtNew, const VARIANT* pSrc = nullptr) noexcept
    {
        VARIANT* pVar = const_cast<VARIANT*>(pSrc);

        if (pVar == nullptr)
        {
            pVar = this;
        }
        // Do nothing if doing in place convert and vts not different
        return ::VariantChangeType(this, pVar, 0, vtNew);
    }
};

///////////////////////////////////////////////////////////////////////
/// Variant helpers
/// 
///

// convert to double by correctly handling the floating point separator
bool VariantToFloat(VARIANT& var);

// {504FAF37-DAA2-4ABA-823A-481B701F7E55}
const IID IID_IValueCollection = { 0x504faf37, 0xdaa2, 0x4aba, 0x82, 0x3a, 0x48, 0x1b, 0x70, 0x1f, 0x7e, 0x55 };

class ICollectionKey
{
public:
    virtual const char* Name() const noexcept = 0;
    virtual size_t Id() const noexcept = 0;
};

class IValueCollection
    : public IUnknown
{
public:
    virtual bool Has(const ICollectionKey& key) const noexcept = 0;
    virtual Variant Get(const ICollectionKey& key, const Variant def = {}, bool* success = nullptr) const = 0;
    virtual void Set(const ICollectionKey& key, const Variant& value) = 0;
    virtual void Set(const ICollectionKey& key, Variant&& value) = 0;
    virtual void Remove(const ICollectionKey& key) noexcept = 0;
    virtual void Clear() noexcept = 0;
    virtual size_t Count() const noexcept = 0;
    virtual Variant ByIndex(size_t idx, Variant* key = nullptr) const = 0;
};

class ValueCollection
    : public RefCount<IValueCollection>
{
protected:
    using Key = std::pair<size_t, std::string>;
    struct keyCompareLess
    {
        constexpr bool operator()(const Key& left, const Key& right) const noexcept
        {
            return left.first < right.first;
        }
    };
    using Map = std::map<Key, Variant, keyCompareLess>;

public:
    using ValueType = Map::value_type;

    ValueCollection() = default;

    template<class... Args>
    ValueCollection(Args&&... args)
    {
        (m_valueCollection.emplace(std::forward<Args>(args)), ...);
    }

    bool Has(const ICollectionKey& key) const noexcept override
    {
        const Key id{ key.Id(), {} };
        const std::shared_lock<std::shared_mutex> guard(m_mtx);
        return m_valueCollection.find(id) != m_valueCollection.end();
    }

    Variant Get(const ICollectionKey& key, const Variant def = {}, bool* success = nullptr) const override
    {
        const Key id{ key.Id(), {} };
        const std::shared_lock<std::shared_mutex> guard(m_mtx);

        const auto i = m_valueCollection.find(id);

        if (i != m_valueCollection.end())
        {
            if (success)
            {
                *success = true;
            }
            return i->second;
        }
        if (success)
        {
            *success = false;
        }
        return def;
    }

    void Set(const ICollectionKey& key, const Variant& value) override
    {
        const char* name = key.Name();
		const Key id{ key.Id(), name ? name : "" };
        const std::lock_guard<std::shared_mutex> guard(m_mtx);
        m_valueCollection[id] = value;
    }
    
    void Set(const ICollectionKey& key, Variant&& value) override
    {
        const char* name = key.Name();
		const Key id{ key.Id(), name ? name : "" };        
        const std::lock_guard<std::shared_mutex> guard(m_mtx);
        m_valueCollection[id] = std::move(value);
    }

    void Remove(const ICollectionKey& key) noexcept override
    {
        const Key id{ key.Id(), {} };
        const std::lock_guard<std::shared_mutex> guard(m_mtx);
        m_valueCollection.erase(id);
    }

    void Clear() noexcept override
    {
        const std::lock_guard<std::shared_mutex> guard(m_mtx);
        m_valueCollection.clear();       
    }

    size_t Count() const noexcept override
    {
        const std::shared_lock<std::shared_mutex> guard(m_mtx);
        return m_valueCollection.size();
    }

    Variant ByIndex(size_t idx, Variant* key = nullptr) const override
    {
        const std::shared_lock<std::shared_mutex> guard(m_mtx);

        if (idx < m_valueCollection.size())
        {
            auto i = m_valueCollection.begin();

            std::advance(i, idx);

            if (key)
            {
                if (!i->first.second.empty())
                {
                    *key = i->first.second;
                }
                else
                {
                    *key = i->first.first;
                }
            }
            return i->second;
        }
        if (key)
        {
            key->Clear();
        }
        return {};
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(const IID& iid, void** ppv) override
    {
        assert(ppv);
        *ppv = nullptr;

        if (::InlineIsEqualGUID(iid, IID_IValueCollection))
        {
            *ppv = (IUnknown*)this;
        }
        if (*ppv)
        {
            ((IUnknown*)*ppv)->AddRef();
            return S_OK;
        }
        return RefCount<IValueCollection>::QueryInterface(iid, ppv);
    }

protected:
    Map m_valueCollection;
    mutable std::shared_mutex m_mtx;
};

// {20F4ADE7-B465-4E3A-BFE9-E9DAA8E62DD9}
const IID IID_IFutureValue = { 0x20f4ade7, 0xb465, 0x4e3a, 0xbf, 0xe9, 0xe9, 0xda, 0xa8, 0xe6, 0x2d, 0xd9 };

class IFutureValue 
    : public IUnknown
{
public:
    virtual std::shared_future<Variant> GetSharedFuture() noexcept = 0;
    virtual Variant Get() = 0;
    virtual void Set(const Variant& value) = 0;
    virtual bool Wait(unsigned int ms) const = 0;
};

class FutureValue
    : public RefCount<IFutureValue>
{
public:
    FutureValue(std::future<Variant>&& f)
        : m_f(std::move(f))
    {
    }

    FutureValue()
    {
        m_f = m_p.get_future();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(const IID& iid, void** ppv) override
    {
        assert(ppv);
        *ppv = nullptr;

        if (::InlineIsEqualGUID(iid, IID_IFutureValue))
        {
            *ppv = (IUnknown*)this;
        }
        if (*ppv)
        {
            ((IUnknown*)*ppv)->AddRef();
            return S_OK;
        }
        return RefCount<IFutureValue>::QueryInterface(iid, ppv);
    }

    std::shared_future<Variant> GetSharedFuture() noexcept override
    {
        return m_f.share();
    }

    Variant Get() override
    {
        const std::lock_guard guard{ m_mtx };

        if (!m_hasValue)
        {
            m_value     = m_f.get();
            m_hasValue  = true;
        }
        return m_value;
    }

    void Set(const Variant& value) override
    {
        const std::lock_guard guard{ m_mtx };
        m_p.set_value(value);
    }

    bool Wait(unsigned int ms) const override
    {
        if (m_hasValue)
        {
            return true;
        }
        try
        {
            return m_f.wait_for(std::chrono::milliseconds(ms)) == std::future_status::ready;
        }
        catch (...)
        {
        }
        return true;
    }

private:
    Variant                         m_value;
    std::atomic_bool                m_hasValue{ false };
    mutable std::mutex              m_mtx;
    mutable std::future<Variant>    m_f;
    std::promise<Variant>           m_p;
};

enum class JsonFormat
{
    compact,
    humanreadable
};

bool FromJson(IValueCollection* valueCollection, IStream* jsonStream);

bool ToJson(const IValueCollection* valueCollection, IStream* stream, JsonFormat format = JsonFormat::compact);
SharedPtr<IStream> ToJson(const IValueCollection* valueCollection, JsonFormat format = JsonFormat::compact);

template<class T>
inline T StringifyStream(IStream* stream)
{
    T result;

    if (stream)
    {
        size_t size = BlobStream::GetSize(stream);

        if (size % sizeof(typename T::value_type))
        {
            throw std::runtime_error("has to align to the item size");
        }
        result.resize(static_cast<typename T::size_type>(size / sizeof(typename T::value_type)));

		if (FAILED(stream->Seek({}, STREAM_SEEK_SET, nullptr)))
        {
            assert(false);
        }

        ULONG read = 0;
        if (S_OK != stream->Read(result.data(), static_cast<ULONG>(size), &read) || read != static_cast<ULONG>(size))
        {
            throw std::runtime_error("failed to read stream");
        }
    }
    return result;
}

template<class T>
inline T Stringify(IUnknown* pUnk)
{
    T result;

    if (pUnk)
    {
        SharedPtr<IStream> stream;
        SharedPtr<IValueCollection> valueCollection;

        if (SUCCEEDED(pUnk->QueryInterface(IID_IValueCollection, (void**)&valueCollection.p)))
        {
            stream = ToJson(valueCollection);
        }
        else
        {
            pUnk->QueryInterface(IID_IStream, (void**)&stream.p);
        }
        if (stream.IsValid())
        {
            result = StringifyStream<T>(stream);
        }
    }
    return result;
}

namespace VariantValue
{
    template<class T>
	inline T Get(const VARIANT& var)
	{
		T result{};

        switch (var.vt)
        {
        case VT_NULL:
        case VT_EMPTY:
        {
        }
        break;

        case VT_UNKNOWN:
        case VT_DISPATCH:
        {
            if (var.punkVal)
            {
                SharedPtr<IFutureValue> futureValue;

                if (SUCCEEDED(var.punkVal->QueryInterface(IID_IFutureValue, (void**)&futureValue.p)))
                {
                    result = Get<T>(futureValue->Get());
                }
            }
        }
        break;

        case VT_BSTR:
        {
            Variant temp(var);

            if (VariantToFloat(temp))
            {
                result = static_cast<T>(temp.dblVal);
            }
            else
            {
                result = static_cast<T>(std::stod(var.bstrVal));
            }
        }
        break;

        case VT_DATE:
        case VT_R8:
        {
            result = static_cast<T>(var.dblVal);
        }
        break;

        case VT_R4:
        {
            result = static_cast<T>(var.fltVal);
        }
        break;

        case VT_BOOL:
        {
            result = static_cast<T>(var.boolVal != VARIANT_FALSE ? 1 : 0);
        }
        break;

        case VT_I1:
        {
            result = static_cast<T>(var.cVal);
        }
        break;

        case VT_UI1:
        {
            result = static_cast<T>(var.bVal);
        }
        break;

        case VT_I2:
        {
            result = static_cast<T>(var.iVal);
        }
        break;

        case VT_UI2:
        {
            result = static_cast<T>(var.uiVal);
        }
        break;

        case VT_I4:
        {
            result = static_cast<T>(var.lVal);
        }
        break;

        case VT_UI4:
        {
            result = static_cast<T>(var.ulVal);
        }
        break;

        case VT_I8:
        {
            result = static_cast<T>(var.llVal);
        }
        break;

        case VT_UI8:
        {
            result = static_cast<T>(var.ullVal);
        }
        break;

        default:
        {
            assert(false);
        }
        break;
        }
        return result;
    }
    
    template<>
	inline std::wstring Get(const VARIANT& var)
	{
		std::wstring result;

        switch (var.vt)
        {
        case VT_NULL:
        case VT_EMPTY:
        {
        }
        break;

        case VT_UNKNOWN:
        case VT_DISPATCH:
        {
            if (var.punkVal)
            {
                SharedPtr<IFutureValue> futureValue;

                if (SUCCEEDED(var.punkVal->QueryInterface(IID_IFutureValue, (void**)&futureValue.p)))
                {
                    result = Get<std::wstring>(futureValue->Get());
                }
                else
                {
                    result = Stringify<std::wstring>(var.punkVal);
                }
            }
        }
        break;

        case VT_BSTR:
        {
            result = var.bstrVal;
        }
        break;

        case VT_DATE:
        case VT_R8:
        {
            result = std::to_wstring(var.dblVal);
        }
        break;

        case VT_R4:
        {
            result = std::to_wstring(var.fltVal);
        }
        break;

        case VT_BOOL:
        {
            result = var.boolVal != VARIANT_FALSE ? L"true" : L"false";
        }
        break;

        case VT_I1:
        {
            result = std::to_wstring(var.cVal);
        }
        break;

        case VT_UI1:
        {
            result = std::to_wstring(var.bVal);
        }
        break;

        case VT_I2:
        {
            result = std::to_wstring(var.iVal);
        }
        break;

        case VT_UI2:
        {
            result = std::to_wstring(var.uiVal);
        }
        break;

        case VT_I4:
        {
            result = std::to_wstring(var.lVal);
        }
        break;

        case VT_UI4:
        {
            result = std::to_wstring(var.ulVal);
        }
        break;

        case VT_I8:
        {
            result = std::to_wstring(var.llVal);
        }
        break;

        case VT_UI8:
        {
            result = std::to_wstring(var.ullVal);
        }
        break;

        default:
        {
            assert(false);
        }
        break;
        }
        return result;
    }

    template<>
	inline std::string Get(const VARIANT& var)
	{
		std::string result;

        switch (var.vt)
        {
        case VT_NULL:
        case VT_EMPTY:
        {
        }
        break;

        case VT_UNKNOWN:
        case VT_DISPATCH:
        {
            if (var.punkVal)
            {
                SharedPtr<IFutureValue> futureValue;

                if (SUCCEEDED(var.punkVal->QueryInterface(IID_IFutureValue, (void**)&futureValue.p)))
                {
                    result = Get<std::string>(futureValue->Get());
                }
                else
                {           
                    result = Stringify<std::string>(var.punkVal);
                }
            }
        }
        break;

        case VT_BSTR:
        {
            result = CW2AEX(var.bstrVal);
        }
        break;

        case VT_DATE:
        case VT_R8:
        {
            result = std::to_string(var.dblVal);
        }
        break;

        case VT_R4:
        {
            result = std::to_string(var.fltVal);
        }
        break;

        case VT_BOOL:
        {
            result = var.boolVal != VARIANT_FALSE ? "true" : "false";
        }
        break;

        case VT_I1:
        {
            result = std::to_string(var.cVal);
        }
        break;

        case VT_UI1:
        {
            result = std::to_string(var.bVal);
        }
        break;

        case VT_I2:
        {
            result = std::to_string(var.iVal);
        }
        break;

        case VT_UI2:
        {
            result = std::to_string(var.uiVal);
        }
        break;

        case VT_I4:
        {
            result = std::to_string(var.lVal);
        }
        break;

        case VT_UI4:
        {
            result = std::to_string(var.ulVal);
        }
        break;

        case VT_I8:
        {
            result = std::to_string(var.llVal);
        }
        break;

        case VT_UI8:
        {
            result = std::to_string(var.ullVal);
        }
        break;

        default:
        {
            assert(false);
        }
        break;
        }
        return result;
    }

    template<>
	inline std::vector<BYTE> Get(const VARIANT& var)
	{
		std::vector<BYTE> result;

        switch (var.vt)
        {
        case VT_NULL:
        case VT_EMPTY:
        {
        }
        break;

        case VT_UNKNOWN:
        case VT_DISPATCH:
        {
            if (var.punkVal)
            {
                SharedPtr<IFutureValue> futureValue;

                if (SUCCEEDED(var.punkVal->QueryInterface(IID_IFutureValue, (void**)&futureValue.p)))
                {
                    result = Get<std::vector<BYTE>>(futureValue->Get());
                }
                else
                {
                    result = Stringify<std::vector<BYTE>>(var.punkVal);
                }
            }
        }
        break;

        case VT_BSTR:
        {
            if (var.bstrVal)
            {
                const size_t size = wcslen(var.bstrVal) * sizeof(wchar_t);

                result.resize(size);
                memcpy(result.data(), var.bstrVal, size);
            }
        }
        break;

        case VT_DATE:
        case VT_R8:
        {
        }
        break;

        case VT_R4:
        {
        }
        break;

        case VT_BOOL:
        {
        }
        break;

        case VT_I1:
        {
        }
        break;

        case VT_UI1:
        {
        }
        break;

        case VT_I2:
        {
        }
        break;

        case VT_UI2:
        {
        }
        break;

        case VT_I4:
        {
        }
        break;

        case VT_UI4:
        {
        }
        break;

        case VT_I8:
        {
        }
        break;

        case VT_UI8:
        {
        }
        break;

        default:
        {
            assert(false);
        }
        break;
        }
        return result;
    }

    template<>
	inline bool Get(const VARIANT& var)
	{
		bool result = false;

        switch (var.vt)
        {
        case VT_NULL:
        case VT_EMPTY:
        {
        }
        break;

        case VT_UNKNOWN:
        case VT_DISPATCH:
        {
            if (var.punkVal)
            {
                SharedPtr<IFutureValue> futureValue;

                if (SUCCEEDED(var.punkVal->QueryInterface(IID_IFutureValue, (void**)&futureValue.p)))
                {
                    result = Get<bool>(futureValue->Get());
                }
                else
                {
                    result = true;
                }
            }
        }
        break;

        case VT_BSTR:
        {
            result = wcscmp(var.bstrVal, L"false") != 0 ? true : false;
        }
        break;

        case VT_DATE:
        case VT_R8:
        {
            result = !!var.dblVal;
        }
        break;

        case VT_R4:
        {
            result = !!var.fltVal;
        }
        break;

        case VT_BOOL:
        {
            result = var.boolVal != VARIANT_FALSE ? true : false;
        }
        break;

        case VT_I1:
        {
            result = !!var.cVal;
        }
        break;

        case VT_UI1:
        {
            result = !!var.bVal;
        }
        break;

        case VT_I2:
        {
            result = !!var.iVal;
        }
        break;

        case VT_UI2:
        {
            result = !!var.uiVal;
        }
        break;

        case VT_I4:
        {
            result = !!var.lVal;
        }
        break;

        case VT_UI4:
        {
            result = !!var.ulVal;
        }
        break;

        case VT_I8:
        {
            result = !!var.llVal;
        }
        break;

        case VT_UI8:
        {
            result = !!var.ullVal;
        }
        break;

        default:
        {
            assert(false);
        }
        break;
        }
        return result;
    }

    template<>
	inline double Get(const VARIANT& var)
	{
		double result{};

        switch (var.vt)
        {
        case VT_NULL:
        case VT_EMPTY:
        {
        }
        break;

        case VT_UNKNOWN:
        case VT_DISPATCH:
        {
            if (var.punkVal)
            {
                SharedPtr<IFutureValue> futureValue;

                if (SUCCEEDED(var.punkVal->QueryInterface(IID_IFutureValue, (void**)&futureValue.p)))
                {
                    result = Get<double>(futureValue->Get());
                }
            }
        }
        break;

        case VT_BSTR:
        {
            Variant temp(var);

            if (VariantToFloat(temp))
            {
                result = temp.dblVal;
            }
            else
            {
                result = std::stod(var.bstrVal);
            }
        }
        break;

        case VT_DATE:
        case VT_R8:
        {
            result = var.dblVal;
        }
        break;

        case VT_R4:
        {
            result = static_cast<double>(var.fltVal);
        }
        break;

        case VT_BOOL:
        {
            result = static_cast<double>(var.boolVal != VARIANT_FALSE ? 1 : 0);
        }
        break;

        case VT_I1:
        {
            result = static_cast<double>(var.cVal);
        }
        break;

        case VT_UI1:
        {
            result = static_cast<double>(var.bVal);
        }
        break;

        case VT_I2:
        {
            result = static_cast<double>(var.iVal);
        }
        break;

        case VT_UI2:
        {
            result = static_cast<double>(var.uiVal);
        }
        break;

        case VT_I4:
        {
            result = static_cast<double>(var.lVal);
        }
        break;

        case VT_UI4:
        {
            result = static_cast<double>(var.ulVal);
        }
        break;

        case VT_I8:
        {
            result = static_cast<double>(var.llVal);
        }
        break;

        case VT_UI8:
        {
            result = static_cast<double>(var.ullVal);
        }
        break;

        default:
        {
            assert(false);
        }
        break;
        }
        return result;
    }

    template<>
	inline SharedPtr<IValueCollection> Get(const VARIANT& var)
	{
		SharedPtr<IValueCollection> result;

        switch (var.vt)
        {
        case VT_UNKNOWN:
        case VT_DISPATCH:
        {
            if (var.punkVal)
            {
                SharedPtr<IFutureValue> futureValue;

                if (SUCCEEDED(var.punkVal->QueryInterface(IID_IFutureValue, (void**)&futureValue.p)))
                {
                    result = Get<SharedPtr<IValueCollection>>(futureValue->Get());
                }
                else
                {				
                    SharedPtr<IStream> stream;

                    if (SUCCEEDED(var.punkVal->QueryInterface(IID_IStream, (void**)&stream.p)))
                    {
                        result = MakeShared<ValueCollection>();

                        if (SUCCEEDED(stream->Seek({}, STREAM_SEEK_SET, nullptr)))
                        {
                            size_t id = 0;
                            BYTE byte;

                            while (S_OK == stream->Read(&byte, 1, nullptr))
                            {
                                class CollectionKey
                                    : public ICollectionKey
                                {
                                public:
                                    constexpr CollectionKey(size_t id) noexcept
                                        : m_id{ id }
                                    {
                                    }

                                    const char* Name() const noexcept override
                                    {
                                        return "";
                                    }

                                    size_t Id() const noexcept override
                                    {
                                        return m_id;
                                    }

                                private:
                                    const size_t m_id;
                                };
                                result->Set(CollectionKey(id++), byte);
                            }
                        }
                    }
                    else
                    {
                        var.punkVal->QueryInterface(IID_IValueCollection, (void**)&result.p);
                    }
                }
            }
        }
        break;
        }
        return result;
    }

    template<>
	inline SharedPtr<IStream> Get(const VARIANT& var)
	{
		SharedPtr<IStream> result;

        switch (var.vt)
        {
        case VT_UNKNOWN:
        case VT_DISPATCH:
        {
            if (var.punkVal)
            {
                SharedPtr<IFutureValue> futureValue;

                if (SUCCEEDED(var.punkVal->QueryInterface(IID_IFutureValue, (void**)&futureValue.p)))
                {
                    result = Get<SharedPtr<IStream>>(futureValue->Get());
                }
                else
                {               
                    var.punkVal->QueryInterface(IID_IStream, (void**)&result.p);
                }
            }
        }
        break;
        }
        return result;
    }

    template<>
	inline SharedPtr<IUnknown> Get(const VARIANT& var)
	{
		SharedPtr<IUnknown> result;

        switch (var.vt)
        {
        case VT_UNKNOWN:
        case VT_DISPATCH:
        {
            if (var.punkVal)
            {
                SharedPtr<IFutureValue> futureValue;

                if (SUCCEEDED(var.punkVal->QueryInterface(IID_IFutureValue, (void**)&futureValue.p)))
                {
                    result = Get<SharedPtr<IUnknown>>(futureValue->Get());
                }
                else
                {
                    var.punkVal->QueryInterface(IID_IUnknown, (void**)&result.p);
                }
            }
        }
        break;
        }
        return result;
    }

    template<>
	inline Variant Get(const VARIANT& var)
	{
        switch (var.vt)
        {
        case VT_UNKNOWN:
        case VT_DISPATCH:
        {
            if (var.punkVal)
            {
                SharedPtr<IFutureValue> futureValue;

                if (SUCCEEDED(var.punkVal->QueryInterface(IID_IFutureValue, (void**)&futureValue.p)))
                {
                    return futureValue->Get();
                }
            }
        }
        break;
        }        
        return var;
    }

    template<>
    inline SharedPtr<IFutureValue> Get(const VARIANT& var)
    {
        SharedPtr<IFutureValue> result;

        switch (var.vt)
        {
        case VT_UNKNOWN:
        case VT_DISPATCH:
        {
            if (var.punkVal)
            {
                var.punkVal->QueryInterface(IID_IFutureValue, (void**)&result.p);
            }
        }
        break;
        }
        return result;
    }

    class Key
        : public ICollectionKey
    {
	private:
		const char* m_name;
		const size_t m_id;

    public:
		explicit Key(const char* name) noexcept
			: m_name{ name ? name : "" }
			, m_id{ !name || !*name ? 0 : std::hash<std::string>{}(name) }
		{
		}

        explicit Key(const std::string& name) noexcept
            : m_name{ name.c_str()}
            , m_id{ name.empty() ? 0 : std::hash<std::string>{}(name)}
        {
        }

		constexpr explicit Key(size_t id) noexcept
			: m_name{ "" }
			, m_id{ id }
		{
		}

		constexpr Key(int id) noexcept
			: m_name{ "" }
			, m_id{ static_cast<size_t>(id) }
		{
		}

		Key(const Key&) = delete;
		Key& operator=(Key&) = delete;

		const char* Name() const noexcept override
		{
			return m_name;
		}

		size_t Id() const noexcept override
		{
			return m_id;
		}

		operator size_t() const noexcept
		{
			return m_id;
		}

		operator const char*() const noexcept
		{
			return m_name;
		}

        operator std::pair<size_t, std::string>() const
        {
            return { m_id, m_name };
        }

    public:
        bool Has(const IValueCollection* valueCollection) const noexcept
        {
            if (valueCollection)
            {
                return valueCollection->Has(*this);
            }
            return false;
        }

        void Remove(IValueCollection* valueCollection) const noexcept
        {
            if (valueCollection)
            {
                valueCollection->Remove(*this);
            }
        }

        template<class I>
        bool Has(const I* p) const noexcept
        {
            if (p)
            {
                SharedPtr<IValueCollection> valueCollection;

                if (SUCCEEDED(((I*)p)->QueryInterface(IID_IValueCollection, (void**)&valueCollection.p)))
                {
                    return Has(valueCollection);
                }
            }
            return false;
        }
        
        template<class I>
        void Remove(const I* p) const noexcept
        {
            if (p)
            {
                SharedPtr<IValueCollection> valueCollection;

                if (SUCCEEDED(((I*)p)->QueryInterface(IID_IValueCollection, (void**)&valueCollection.p)))
                {
                    return Remove(valueCollection);
                }
            }
        }

        template<class T>
        T Get(const IValueCollection* valueCollection) const
        {
            if (valueCollection)
            {
                return VariantValue::Get<T>(valueCollection->Get(*this));
            }
            return {};
        }

        template<class T>
        std::optional<T> TryGet(const IValueCollection* valueCollection) const
        {
            if (valueCollection)
            {
                bool success = false;
                const auto v = valueCollection->Get(*this, {}, &success);

                if (success)
                {
                    if (v.vt == VT_UNKNOWN && v.punkVal)
                    {
                        SharedPtr<IFutureValue> futureValue;

                        if (SUCCEEDED(v.punkVal->QueryInterface(IID_IFutureValue, (void**)&futureValue.p)))
                        {
                            if (!futureValue->Wait(0))
                            {
                                return {};
                            }
                        }
                    }
                    return VariantValue::Get<T>(v);
                }
            }
            return {};
        }

        template<class T, class I>
        T Get(const I* p) const
        {
            if (p)
            {
                SharedPtr<IValueCollection> valueCollection;

                if (SUCCEEDED(((I*)p)->QueryInterface(IID_IValueCollection, (void**)&valueCollection.p)))
                {
                    return Get<T>(valueCollection);
                }
            }
            return {};
        }

        template<class T>
		void Set(IValueCollection* valueCollection, const T val) const
		{
			if (valueCollection)
			{
				valueCollection->Set(*this, val);
			}
		}

		void Set(IValueCollection* valueCollection, const nullptr_t&) const
		{
			if (valueCollection)
			{
                Variant varNull;
                varNull.vt = VT_NULL;
				valueCollection->Set(*this, std::move(varNull));
			}
		}

		void Set(IValueCollection* valueCollection, Variant&& val) const
		{
			if (valueCollection)
			{
				valueCollection->Set(*this, std::move(val));
			}
		}

        template<class T>
		void Set(IValueCollection* valueCollection, const SharedPtr<T>& val) const
		{
			if (valueCollection)
			{
				valueCollection->Set(*this, val.p);
			}
		}

        template<class T>
		void Set(IValueCollection* valueCollection, SharedPtr<T>&& val) const
		{
			if (valueCollection)
			{
				valueCollection->Set(*this, val.Detach());
			}
		}

        template<class T>
		void Set(IValueCollection* valueCollection, const std::vector<uint8_t>& val) const
		{
			if (valueCollection)
			{
				valueCollection->Set(*this, val);
			}
		}

        template<class T>
		void Set(IValueCollection* valueCollection, std::vector<uint8_t>&& val) const
		{
			if (valueCollection)
			{
				valueCollection->Set(*this, std::move(val));
			}
		}

        template<class I, class T>
		void Set(I* p, const T val) const
		{
			if (p)
			{
				SharedPtr<IValueCollection> valueCollection;

				if (SUCCEEDED(p->QueryInterface(IID_IValueCollection, (void**)&valueCollection.p)))
				{
					Set(valueCollection.p, val);
				}
			}
		}

        template<class I>
		void Set(I* p, Variant&& val) const
		{
			if (p)
			{
				SharedPtr<IValueCollection> valueCollection;

				if (SUCCEEDED(p->QueryInterface(IID_IValueCollection, (void**)&valueCollection.p)))
				{
					Set(valueCollection.p, std::move(val));
				}
			}
		}
        
        template<class I, class T>
		void Set(I* p, const SharedPtr<T>& val) const
		{
			if (p)
			{
				SharedPtr<IValueCollection> valueCollection;

				if (SUCCEEDED(p->QueryInterface(IID_IValueCollection, (void**)&valueCollection.p)))
				{
					Set(valueCollection.p, val);
				}
			}
		}
        
        template<class I, class T>
		void Set(I* p, SharedPtr<T>&& val) const
		{
			if (p)
			{
				SharedPtr<IValueCollection> valueCollection;

				if (SUCCEEDED(p->QueryInterface(IID_IValueCollection, (void**)&valueCollection.p)))
				{
					Set(valueCollection.p, std::move(val));
				}
			}
		}

        template<class I, class T>
		void Set(I* p, std::vector<uint8_t>& val) const
		{
			if (p)
			{
				SharedPtr<IValueCollection> valueCollection;

				if (SUCCEEDED(p->QueryInterface(IID_IValueCollection, (void**)&valueCollection.p)))
				{
					Set(valueCollection.p, val);
				}
			}
		}
        
        template<class I, class T>
		void Set(I* p, std::vector<uint8_t>&& val) const
		{
			if (p)
			{
				SharedPtr<IValueCollection> valueCollection;

				if (SUCCEEDED(p->QueryInterface(IID_IValueCollection, (void**)&valueCollection.p)))
				{
					Set(valueCollection.p, std::move(val));
				}
			}
		}
    };
} // namespace VariantValue

inline std::string GetHomeDirectoryA()
{
    char path[2049] = { 0 };

    DWORD offset = 0;

#ifdef _WIN32
    offset = GetEnvironmentVariableA(
        "HOMEDRIVE"
        , path, 2048);
#endif

    if (offset >= 2048)
    {
        throw std::runtime_error("buffer overflow");
    }
    GetEnvironmentVariableA(
#ifdef _WIN32
        "HOMEPATH"
#else
        "HOME"
#endif        
        , path+offset, 2048-offset);
    return path;
}