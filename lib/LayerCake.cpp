#include <LayerCake.h>
#include <stack>

#ifndef _WIN32
#include <unistd.h>
#endif

#define RAPIDJSON_HAS_STDSTRING 1
#include "internal_rapidjson/pointer.h"
#include "internal_rapidjson/prettywriter.h"
#include "internal_rapidjson/writer.h"
#include "internal_rapidjson/reader.h"

using namespace std::string_literals;

#ifdef _WIN32

std::wstring CA2WEX(const std::string& input, unsigned int cp /*= CP_UTF8*/)
{
    std::wstring result;

    const int la = static_cast<int>(input.length());

    if (la)
    {
        int lw = la;

        result.resize(lw);

        if (!::MultiByteToWideChar(cp, 0, input.c_str(), la, result.data(), lw))
        {
            if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            {
                lw = ::MultiByteToWideChar(cp, 0, input.c_str(), la, NULL, 0);

                result.resize(lw);

                if (!::MultiByteToWideChar(cp, 0, input.c_str(), la, result.data(), lw))
                {
                    assert(false);
                }
            }
            else
            {
                assert(false);
            }
        }
    }
    return result;
}

std::string CW2AEX(const std::wstring& input, unsigned int cp /*= CP_UTF8*/)
{
    std::string result;

    const int lw = static_cast<int>(input.length());

    if (lw)
    {
        const int la = ::WideCharToMultiByte(cp, WC_ERR_INVALID_CHARS, input.c_str(), lw, nullptr, 0, nullptr, nullptr);

        if (la)
        {
            result.resize(la);

            if (la != ::WideCharToMultiByte(cp, WC_ERR_INVALID_CHARS, input.c_str(), lw, result.data(), la, nullptr, nullptr))
            {
                assert(false);
            }
        }
        else
        {
            assert(GetLastError() == ERROR_SUCCESS);
        }
    }
    return result;
}

#endif

char GetFloatingPointSeparator()
{
    static std::atomic_char fpSep{0};

    char cFp = fpSep.load();
    if (cFp)
    {
        return cFp;
    }
    Variant var(L"1.2"s);

    if (SUCCEEDED(var.ChangeType(VT_R8)))
    {
        if (var.dblVal < 1.1 || var.dblVal >= 2.0)
        {
            var = L"1,2";

            if (SUCCEEDED(var.ChangeType(VT_R8)))
            {
                if (var.dblVal > 1.1 && var.dblVal < 2.0)
                {
                    assert(var.dblVal == 1.2);
                    fpSep = ',';
                    return ',';
                }
            }
        }
    }
    assert(var.dblVal == 1.2);
    fpSep = '.';
    return '.';
}

bool VariantToFloat(VARIANT& var)
{
    if (var.vt == VT_BSTR)
    {
        std::wstring strVal = var.bstrVal;

        const auto sep = strVal.find_first_not_of(L"0123456789-+eE"s);

        if (sep != std::wstring::npos)
        {
            const char fpSep = GetFloatingPointSeparator();

            if (fpSep != static_cast<char>(strVal[sep]))
            {
                *(var.bstrVal + sep) = fpSep;
            }
        }
    }
    return SUCCEEDED(::VariantChangeType(&var, &var, 0, VT_R8));
}

#ifndef _WIN32

const IID GUID_NULL					= { 0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const IID IID_IUnknown				= { 0x00000000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 };
const IID IID_IStream				= { 0x0000000c, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 };
const IID IID_IPersistStream		= { 0x00000109, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 };
const IID IID_IPersistStreamInit	= { 0x7FD52380, 0x4E07, 0x101B, 0xAE, 0x2D, 0x08, 0x00, 0x2B, 0x2E, 0xC7, 0x13 };
const IID IID_ISequentialStream		= { 0x0c733a30, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d };

bool CreateProcess(const char* cmd, FILE** readPipe, FILE** writePipe /*= NULL*/)
{
    bool    result = false;
    int     fd1[2];
    int     fd2[2];
    int     ispipe1 = -1;
    int     ispipe2 = -1;
    pid_t   cpid = 0;
    FILE*   wp = NULL;

    // open two pipes: Pipe1: parent reads, pipe2_: parent writes
    if ((ispipe1 = pipe(fd1)) < 0 || (ispipe2 = pipe(fd2)) < 0)
    {
        return false;
    }

    switch (cpid = fork())
    {
    case -1:
        return false;

    case 0:
    {
        // Child
        dup2(fd2[0], 0);
        dup2(fd1[1], 1);

        close(fd1[0]);
        close(fd1[1]);
        close(fd2[0]);
        close(fd2[1]);

        execlp("/bin/sh", "sh", "-c", cmd, NULL);
    }
    break;

    default:
    {
        // parent
        if ((*readPipe = fdopen(fd1[0], "r")) == NULL || (wp = fdopen(fd2[1], "w")) == NULL)
        {
            result = false;
        }
        else
        {
            result = true;

            if (writePipe)
            {
                *writePipe = wp;
            }
            else
            {
                fclose(wp);
                wp = NULL;
            }
        }
    }
    break;
    }

    if (ispipe1 >= 0)
    {
        close(fd1[1]);

        if (!result)
        {
            close(fd1[0]);
        }
    }
    if (ispipe2 >= 0)
    {
        close(fd2[0]);

        if (!result || !wp)
        {
            close(fd2[1]);
        }
    }
    return result;
}

BSTR SysAllocString(const OLECHAR* psz)
{
    BSTR result = NULL;

    if (psz)
    {
        const size_t sizeInBytes = (wcslen(psz) + 1) * sizeof(OLECHAR);

        result = (BSTR)malloc(sizeInBytes);

        if (result)
        {
            memcpy(result, psz, sizeInBytes);
        }
    }
    return result;
}

BSTR SysAllocStringLen(const OLECHAR* psz, size_t len_in_chars)
{
    BSTR result = NULL;

    if (psz)
    {
        const size_t sizeInBytes = (len_in_chars + 1) * sizeof(OLECHAR);

        result = (BSTR)malloc(sizeInBytes);

        if (result)
        {
            memcpy(result, psz, sizeInBytes - sizeof(OLECHAR));
            *(result + len_in_chars) = L'\0';
        }
    }
    return result;
}

HRESULT CreateStreamOnHGlobal(HGLOBAL hGlobal, BOOL fDeleteOnRelease, LPSTREAM* ppstm)
{
    assert(!hGlobal);
    assert(fDeleteOnRelease);
    assert(ppstm);

    try
    {
        *ppstm = new BlobStream;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
    (*ppstm)->AddRef();

    return S_OK;
}

HRESULT VariantClear(PVARIANT var)
{
    if (var)
    {
        switch (var->vt)
        {
        case VT_BSTR:
        {
            SysFreeString(var->bstrVal);
        }
        break;

        case VT_UNKNOWN:
        {
            if (var->punkVal)
                var->punkVal->Release();
        }
        break;

        case VT_DISPATCH:
        {
            if (var->pdispVal)
                var->pdispVal->Release();
        }
        break;
        }
        memset(var, 0, sizeof(VARIANT));
    }
    return S_OK;
}

HRESULT VariantCopyInd(VARIANT* pvarDest, const VARIANTARG* pvargSrc)
{
    assert(pvarDest);
    assert(pvargSrc);

    if (pvarDest != pvargSrc)
    {
        ::VariantClear(pvarDest);

        memcpy(pvarDest, pvargSrc, sizeof(VARIANT));

        switch (pvarDest->vt)
        {
        case VT_DISPATCH:
        {
            if (pvarDest->pdispVal)
            {
                pvarDest->pdispVal->AddRef();
            }
        }
        break;

        case VT_UNKNOWN:
        {
            if (pvarDest->punkVal)
            {
                pvarDest->punkVal->AddRef();
            }
        }
        break;

        case VT_BSTR:
        {
            if (pvargSrc->bstrVal)
            {
                pvarDest->bstrVal = SysAllocString(pvargSrc->bstrVal);
            }
        }
        break;
        }
    }
    return S_OK;
}

HRESULT VariantChangeType(VARIANTARG* pvargDest, const VARIANTARG* pvarSrc, USHORT wFlags, VARTYPE vt)
{
    assert(pvarSrc);
    assert(pvargDest);

    if (pvargDest != pvarSrc)
    {
        ::VariantClear(pvargDest);
    }
    else if (pvarSrc->vt == vt)
    {
        return S_OK;
    }

    switch (pvarSrc->vt)
    {
    case VT_EMPTY:
    case VT_NULL:
    {
        memset(pvargDest, 0, sizeof(VARIANT));
        pvargDest->vt = vt;
    }
    break;

    case VT_UNKNOWN:
    case VT_DISPATCH:
    {
        assert(false);
        return E_INVALIDARG;
    }
    break;

    case VT_BOOL:
    {
        switch (vt)
        {
        case VT_EMPTY:
        case VT_NULL:
        {
            memset(pvargDest, 0, sizeof(VARIANT));
            pvargDest->vt = vt;
        }
        break;
        
        case VT_BOOL:
        {
            pvargDest->boolVal = pvarSrc->boolVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I1:
        {
            pvargDest->cVal = pvarSrc->boolVal ? 1 : 0;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI1:
        {
            pvargDest->bVal = pvarSrc->boolVal ? 1 : 0;
            pvargDest->vt = vt;
        }
        break;
        case VT_I2:
        {
            pvargDest->iVal = pvarSrc->boolVal ? 1 : 0;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI2:
        {
            pvargDest->uiVal = pvarSrc->boolVal ? 1 : 0;
            pvargDest->vt = vt;
        }
        break;
        case VT_I4:
        case VT_INT:
        {
            pvargDest->lVal = pvarSrc->boolVal ? 1 : 0;
            pvargDest->vt = VT_I4;
        }
        break;
        case VT_UI4:
        case VT_UINT:
        {
            pvargDest->ulVal = pvarSrc->boolVal ? 1 : 0;
            pvargDest->vt = VT_UI4;
        }
        break;
        case VT_I8:
        {
            pvargDest->llVal = pvarSrc->boolVal ? 1 : 0;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI8:
        {
            pvargDest->ullVal = pvarSrc->boolVal ? 1 : 0;
            pvargDest->vt = vt;
        }
        break;
        case VT_R4:
        {
            pvargDest->fltVal = pvarSrc->boolVal ? 1 : 0;
            pvargDest->vt = vt;
        }
        break;
        case VT_R8:
        {
            pvargDest->dblVal = pvarSrc->boolVal ? 1 : 0;
            pvargDest->vt = vt;
        }
        break;
        case VT_BSTR:
        {
            if (wFlags == VARIANT_ALPHABOOL)
            {
                pvargDest->bstrVal = pvarSrc->boolVal ? SysAllocString(L"true") : SysAllocString(L"false");
            }
            else
            {
                pvargDest->bstrVal = pvarSrc->boolVal ? SysAllocString(L"1") : SysAllocString(L"0");
            }
            pvargDest->vt = vt;
        }
        break;

        default:
        {
            assert(false);
            return E_INVALIDARG;
        }
        break;
        }
    }
    break;

    case VT_I1:
    {
        switch (vt)
        {
        case VT_EMPTY:
        case VT_NULL:
        {
            memset(pvargDest, 0, sizeof(VARIANT));
            pvargDest->vt = vt;
        }
        break;

        case VT_BOOL:
        {
            pvargDest->boolVal = pvarSrc->cVal ? VARIANT_TRUE : VARIANT_FALSE;
            pvargDest->vt = vt;
        }
        break;
        case VT_I1:
        {
            pvargDest->cVal = pvarSrc->cVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI1:
        {
            pvargDest->bVal = pvarSrc->cVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I2:
        {
            pvargDest->iVal = pvarSrc->cVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI2:
        {
            pvargDest->uiVal = pvarSrc->cVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I4:
        case VT_INT:
        {
            pvargDest->lVal = pvarSrc->cVal;
            pvargDest->vt = VT_I4;
        }
        break;
        case VT_UI4:
        case VT_UINT:
        {
            pvargDest->ulVal = pvarSrc->cVal;
            pvargDest->vt = VT_UI4;
        }
        break;
        case VT_I8:
        {
            pvargDest->llVal = pvarSrc->cVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI8:
        {
            pvargDest->ullVal = pvarSrc->cVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R4:
        {
            pvargDest->fltVal = pvarSrc->cVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R8:
        {
            pvargDest->dblVal = pvarSrc->cVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_BSTR:
        {
            pvargDest->bstrVal = SysAllocString(std::to_wstring(pvarSrc->cVal).c_str());
            pvargDest->vt = vt;
        }
        break;

        default:
        {
            assert(false);
            return E_INVALIDARG;
        }
        break;
        }
    }
    break;

    case VT_UI1:
    {
        switch (vt)
        {
        case VT_EMPTY:
        case VT_NULL:
        {
            memset(pvargDest, 0, sizeof(VARIANT));
            pvargDest->vt = vt;
        }
        break;

        case VT_BOOL:
        {
            pvargDest->boolVal = pvarSrc->bVal ? VARIANT_TRUE : VARIANT_FALSE;
            pvargDest->vt = vt;
        }
        break;
        case VT_I1:
        {
            pvargDest->cVal = pvarSrc->bVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI1:
        {
            pvargDest->bVal = pvarSrc->bVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I2:
        {
            pvargDest->iVal = pvarSrc->bVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI2:
        {
            pvargDest->uiVal = pvarSrc->bVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I4:
        case VT_INT:
        {
            pvargDest->lVal = pvarSrc->bVal;
            pvargDest->vt = VT_I4;
        }
        break;
        case VT_UI4:
        case VT_UINT:
        {
            pvargDest->ulVal = pvarSrc->bVal;
            pvargDest->vt = VT_UI4;
        }
        break;
        case VT_I8:
        {
            pvargDest->llVal = pvarSrc->bVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI8:
        {
            pvargDest->ullVal = pvarSrc->bVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R4:
        {
            pvargDest->fltVal = pvarSrc->bVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R8:
        {
            pvargDest->dblVal = pvarSrc->bVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_BSTR:
        {
            pvargDest->bstrVal = SysAllocString(std::to_wstring(pvarSrc->bVal).c_str());
            pvargDest->vt = vt;
        }
        break;

        default:
        {
            assert(false);
            return E_INVALIDARG;
        }
        break;
        }
    }
    break;

    case VT_I2:
    {
        switch (vt)
        {
        case VT_EMPTY:
        case VT_NULL:
        {
            memset(pvargDest, 0, sizeof(VARIANT));
            pvargDest->vt = vt;
        }
        break;

        case VT_BOOL:
        {
            pvargDest->boolVal = pvarSrc->iVal ? VARIANT_TRUE : VARIANT_FALSE;
            pvargDest->vt = vt;
        }
        break;
        case VT_I1:
        {
            pvargDest->cVal = (CHAR) pvarSrc->iVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI1:
        {
            pvargDest->bVal = (BYTE) pvarSrc->iVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I2:
        {
            pvargDest->iVal = pvarSrc->iVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI2:
        {
            pvargDest->uiVal = pvarSrc->iVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I4:
        case VT_INT:
        {
            pvargDest->lVal = pvarSrc->iVal;
            pvargDest->vt = VT_I4;
        }
        break;
        case VT_UI4:
        case VT_UINT:
        {
            pvargDest->ulVal = pvarSrc->iVal;
            pvargDest->vt = VT_UI4;
        }
        break;
        case VT_I8:
        {
            pvargDest->llVal = pvarSrc->iVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI8:
        {
            pvargDest->ullVal = pvarSrc->iVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R4:
        {
            pvargDest->fltVal = pvarSrc->iVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R8:
        {
            pvargDest->dblVal = pvarSrc->iVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_BSTR:
        {
            pvargDest->bstrVal = SysAllocString(std::to_wstring(pvarSrc->iVal).c_str());
            pvargDest->vt = vt;
        }
        break;

        default:
        {
            assert(false);
            return E_INVALIDARG;
        }
        break;
        }
    }
    break;

    case VT_UI2:
    {
        switch (vt)
        {
        case VT_EMPTY:
        case VT_NULL:
        {
            memset(pvargDest, 0, sizeof(VARIANT));
            pvargDest->vt = vt;
        }
        break;

        case VT_BOOL:
        {
            pvargDest->boolVal = pvarSrc->uiVal ? VARIANT_TRUE : VARIANT_FALSE;
            pvargDest->vt = vt;
        }
        break;
        case VT_I1:
        {
            pvargDest->cVal = (CHAR) pvarSrc->uiVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI1:
        {
            pvargDest->bVal = (BYTE) pvarSrc->uiVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I2:
        {
            pvargDest->iVal = pvarSrc->uiVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI2:
        {
            pvargDest->uiVal = pvarSrc->uiVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I4:
        case VT_INT:
        {
            pvargDest->lVal = pvarSrc->uiVal;
            pvargDest->vt = VT_I4;
        }
        break;
        case VT_UI4:
        case VT_UINT:
        {
            pvargDest->ulVal = pvarSrc->uiVal;
            pvargDest->vt = VT_UI4;
        }
        break;
        case VT_I8:
        {
            pvargDest->llVal = pvarSrc->uiVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI8:
        {
            pvargDest->ullVal = pvarSrc->uiVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R4:
        {
            pvargDest->fltVal = pvarSrc->uiVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R8:
        {
            pvargDest->dblVal = pvarSrc->uiVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_BSTR:
        {
            pvargDest->bstrVal = SysAllocString(std::to_wstring(pvarSrc->uiVal).c_str());
            pvargDest->vt = vt;
        }
        break;

        default:
        {
            assert(false);
            return E_INVALIDARG;
        }
        break;
        }
    }
    break;

    case VT_INT:
    case VT_I4:
    {
        switch (vt)
        {
        case VT_EMPTY:
        case VT_NULL:
        {
            memset(pvargDest, 0, sizeof(VARIANT));
            pvargDest->vt = vt;
        }
        break;

        case VT_BOOL:
        {
            pvargDest->boolVal = pvarSrc->lVal ? VARIANT_TRUE : VARIANT_FALSE;
            pvargDest->vt = vt;
        }
        break;
        case VT_I1:
        {
            pvargDest->cVal = (CHAR) pvarSrc->lVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI1:
        {
            pvargDest->bVal = (BYTE) pvarSrc->lVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I2:
        {
            pvargDest->iVal = (SHORT) pvarSrc->lVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI2:
        {
            pvargDest->uiVal = (USHORT) pvarSrc->lVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I4:
        case VT_INT:
        {
            pvargDest->lVal = pvarSrc->lVal;
            pvargDest->vt = VT_I4;
        }
        break;
        case VT_UI4:
        case VT_UINT:
        {
            pvargDest->ulVal = pvarSrc->lVal;
            pvargDest->vt = VT_UI4;
        }
        break;
        case VT_I8:
        {
            pvargDest->llVal = pvarSrc->lVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI8:
        {
            pvargDest->ullVal = pvarSrc->lVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R4:
        {
            pvargDest->fltVal = (FLOAT) pvarSrc->lVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R8:
        {
            pvargDest->dblVal = pvarSrc->lVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_BSTR:
        {
            pvargDest->bstrVal = SysAllocString(std::to_wstring(pvarSrc->lVal).c_str());
            pvargDest->vt = vt;
        }
        break;

        default:
        {
            assert(false);
            return E_INVALIDARG;
        }
        break;
        }
    }
    break;

    case VT_UINT:
    case VT_UI4:
    {
        switch (vt)
        {
        case VT_EMPTY:
        case VT_NULL:
        {
            memset(pvargDest, 0, sizeof(VARIANT));
            pvargDest->vt = vt;
        }
        break;

        case VT_BOOL:
        {
            pvargDest->boolVal = pvarSrc->ulVal ? VARIANT_TRUE : VARIANT_FALSE;
            pvargDest->vt = vt;
        }
        break;
        case VT_I1:
        {
            pvargDest->cVal = (CHAR) pvarSrc->ulVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI1:
        {
            pvargDest->bVal = (BYTE) pvarSrc->ulVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I2:
        {
            pvargDest->iVal = (SHORT) pvarSrc->ulVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI2:
        {
            pvargDest->uiVal = (USHORT) pvarSrc->ulVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I4:
        case VT_INT:
        {
            pvargDest->lVal = pvarSrc->ulVal;
            pvargDest->vt = VT_I4;
        }
        break;
        case VT_UI4:
        case VT_UINT:
        {
            pvargDest->ulVal = pvarSrc->ulVal;
            pvargDest->vt = VT_UI4;
        }
        break;
        case VT_I8:
        {
            pvargDest->llVal = pvarSrc->ulVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI8:
        {
            pvargDest->ullVal = pvarSrc->ulVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R4:
        {
            pvargDest->fltVal = (FLOAT) pvarSrc->ulVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R8:
        {
            pvargDest->dblVal = pvarSrc->ulVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_BSTR:
        {
            pvargDest->bstrVal = SysAllocString(std::to_wstring(pvarSrc->ulVal).c_str());
            pvargDest->vt = vt;
        }
        break;

        default:
        {
            assert(false);
            return E_INVALIDARG;
        }
        break;
        }
    }
    break;

    case VT_I8:
    {
        switch (vt)
        {
        case VT_EMPTY:
        case VT_NULL:
        {
            memset(pvargDest, 0, sizeof(VARIANT));
            pvargDest->vt = vt;
        }
        break;

        case VT_BOOL:
        {
            pvargDest->boolVal = pvarSrc->llVal ? VARIANT_TRUE : VARIANT_FALSE;
            pvargDest->vt = vt;
        }
        break;
        case VT_I1:
        {
            pvargDest->cVal = (CHAR) pvarSrc->llVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI1:
        {
            pvargDest->bVal = (BYTE) pvarSrc->llVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I2:
        {
            pvargDest->iVal = (SHORT) pvarSrc->llVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI2:
        {
            pvargDest->uiVal = (USHORT) pvarSrc->llVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I4:
        case VT_INT:
        {
            pvargDest->lVal = (LONG) pvarSrc->llVal;
            pvargDest->vt = VT_I4;
        }
        break;
        case VT_UI4:
        case VT_UINT:
        {
            pvargDest->ulVal = (ULONG) pvarSrc->llVal;
            pvargDest->vt = VT_UI4;
        }
        break;
        case VT_I8:
        {
            pvargDest->llVal = pvarSrc->llVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI8:
        {
            pvargDest->ullVal = pvarSrc->llVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R4:
        {
            pvargDest->fltVal = (FLOAT) pvarSrc->llVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R8:
        {
            pvargDest->dblVal = (DOUBLE) pvarSrc->llVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_BSTR:
        {
            pvargDest->bstrVal = SysAllocString(std::to_wstring(pvarSrc->llVal).c_str());
            pvargDest->vt = vt;
        }
        break;

        default:
        {
            assert(false);
            return E_INVALIDARG;
        }
        break;
        }
    }
    break;

    case VT_UI8:
    {
        switch (vt)
        {
        case VT_EMPTY:
        case VT_NULL:
        {
            memset(pvargDest, 0, sizeof(VARIANT));
            pvargDest->vt = vt;
        }
        break;

        case VT_BOOL:
        {
            pvargDest->boolVal = pvarSrc->ullVal ? VARIANT_TRUE : VARIANT_FALSE;
            pvargDest->vt = vt;
        }
        break;
        case VT_I1:
        {
            pvargDest->cVal = (CHAR) pvarSrc->ullVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI1:
        {
            pvargDest->bVal = (BYTE) pvarSrc->ullVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I2:
        {
            pvargDest->iVal = (SHORT) pvarSrc->ullVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI2:
        {
            pvargDest->uiVal = (USHORT) pvarSrc->ullVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I4:
        case VT_INT:
        {
            pvargDest->lVal = (LONG) pvarSrc->ullVal;
            pvargDest->vt = VT_I4;
        }
        break;
        case VT_UI4:
        case VT_UINT:
        {
            pvargDest->ulVal = (ULONG) pvarSrc->ullVal;
            pvargDest->vt = VT_UI4;
        }
        break;
        case VT_I8:
        {
            pvargDest->llVal = pvarSrc->ullVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI8:
        {
            pvargDest->ullVal = pvarSrc->ullVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R4:
        {
            pvargDest->fltVal = (FLOAT) pvarSrc->ullVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R8:
        {
            pvargDest->dblVal = (DOUBLE) pvarSrc->ullVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_BSTR:
        {
            pvargDest->bstrVal = SysAllocString(std::to_wstring(pvarSrc->ullVal).c_str());
            pvargDest->vt = vt;
        }
        break;

        default:
        {
            assert(false);
            return E_INVALIDARG;
        }
        break;
        }
    }
    break;

    case VT_R4:
    {
        switch (vt)
        {
        case VT_EMPTY:
        case VT_NULL:
        {
            memset(pvargDest, 0, sizeof(VARIANT));
            pvargDest->vt = vt;
        }
        break;

        case VT_BOOL:
        {
            pvargDest->boolVal = pvarSrc->fltVal ? VARIANT_TRUE : VARIANT_FALSE;
            pvargDest->vt = vt;
        }
        break;
        case VT_I1:
        {
            pvargDest->cVal = (CHAR) pvarSrc->fltVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI1:
        {
            pvargDest->bVal = (BYTE) pvarSrc->fltVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I2:
        {
            pvargDest->iVal = (SHORT) pvarSrc->fltVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI2:
        {
            pvargDest->uiVal = (USHORT) pvarSrc->fltVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I4:
        case VT_INT:
        {
            pvargDest->lVal = (LONG) pvarSrc->fltVal;
            pvargDest->vt = VT_I4;
        }
        break;
        case VT_UI4:
        case VT_UINT:
        {
            pvargDest->ulVal = (ULONG) pvarSrc->fltVal;
            pvargDest->vt = VT_UI4;
        }
        break;
        case VT_I8:
        {
            pvargDest->llVal = (LONGLONG) pvarSrc->fltVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI8:
        {
            pvargDest->ullVal = (ULONGLONG) pvarSrc->fltVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R4:
        {
            pvargDest->fltVal = pvarSrc->fltVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R8:
        {
            pvargDest->dblVal = pvarSrc->fltVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_BSTR:
        {
            pvargDest->bstrVal = SysAllocString(std::to_wstring(pvarSrc->fltVal).c_str());
            pvargDest->vt = vt;
        }
        break;

        default:
        {
            assert(false);
            return E_INVALIDARG;
        }
        break;
        }
    }
    break;

    case VT_R8:
    {
        switch (vt)
        {
        case VT_EMPTY:
        case VT_NULL:
        {
            memset(pvargDest, 0, sizeof(VARIANT));
            pvargDest->vt = vt;
        }
        break;

        case VT_BOOL:
        {
            pvargDest->boolVal = pvarSrc->dblVal ? VARIANT_TRUE : VARIANT_FALSE;
            pvargDest->vt = vt;
        }
        break;
        case VT_I1:
        {
            pvargDest->cVal = (CHAR) pvarSrc->dblVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI1:
        {
            pvargDest->bVal = (BYTE) pvarSrc->dblVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I2:
        {
            pvargDest->iVal = (SHORT) pvarSrc->dblVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI2:
        {
            pvargDest->uiVal = (USHORT) pvarSrc->dblVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_I4:
        case VT_INT:
        {
            pvargDest->lVal = (LONG) pvarSrc->dblVal;
            pvargDest->vt = VT_I4;
        }
        break;
        case VT_UI4:
        case VT_UINT:
        {
            pvargDest->ulVal = (ULONG) pvarSrc->dblVal;
            pvargDest->vt = VT_UI4;
        }
        break;
        case VT_I8:
        {
            pvargDest->llVal = (LONGLONG) pvarSrc->dblVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_UI8:
        {
            pvargDest->ullVal = (ULONGLONG) pvarSrc->dblVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R4:
        {
            pvargDest->fltVal = (FLOAT) pvarSrc->dblVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_R8:
        {
            pvargDest->dblVal = pvarSrc->dblVal;
            pvargDest->vt = vt;
        }
        break;
        case VT_BSTR:
        {
            pvargDest->bstrVal = SysAllocString(std::to_wstring(pvarSrc->dblVal).c_str());
            pvargDest->vt = vt;
        }
        break;

        default:
        {
            assert(false);
            return E_INVALIDARG;
        }
        break;
        }
    }
    break;

    case VT_BSTR:
    {
        BSTR toFree = NULL;

        if (pvarSrc == pvargDest)
            toFree = pvarSrc->bstrVal;

        switch (vt)
        {
        case VT_EMPTY:
        case VT_NULL:
        {
            memset(pvargDest, 0, sizeof(VARIANT));
            pvargDest->vt = vt;
        }
        break;

        case VT_BOOL:
        {
            if (!pvarSrc->bstrVal || wcslen(pvarSrc->bstrVal) == 0)
            {
                pvargDest->boolVal = VARIANT_FALSE;
            }
            else
            {
                pvargDest->boolVal = (*pvarSrc->bstrVal != L'f' && *pvarSrc->bstrVal != L'F' && *pvarSrc->bstrVal == L'0') ? VARIANT_TRUE : VARIANT_FALSE;
            }
            pvargDest->vt = vt;
        }
        break;
        case VT_I1:
        {
            if (!pvarSrc->bstrVal || wcslen(pvarSrc->bstrVal) == 0)
            {
                pvargDest->cVal = 0;
            }
            else
            {
                pvargDest->cVal = (CHAR) atol(CW2AEX(pvarSrc->bstrVal).c_str());
            }
            pvargDest->vt = vt;
        }
        break;
        case VT_UI1:
        {
            if (!pvarSrc->bstrVal || wcslen(pvarSrc->bstrVal) == 0)
            {
                pvargDest->bVal = 0;
            }
            else
            {
                pvargDest->bVal = (BYTE) atol(CW2AEX(pvarSrc->bstrVal).c_str());
            }
            pvargDest->vt = vt;
        }
        break;
        case VT_I2:
        {
            if (!pvarSrc->bstrVal || wcslen(pvarSrc->bstrVal) == 0)
            {
                pvargDest->iVal = 0;
            }
            else
            {
                pvargDest->iVal = (SHORT) atol(CW2AEX(pvarSrc->bstrVal).c_str());
            }
            pvargDest->vt = vt;
        }
        break;
        case VT_UI2:
        {
            if (!pvarSrc->bstrVal || wcslen(pvarSrc->bstrVal) == 0)
            {
                pvargDest->uiVal = 0;
            }
            else
            {
                pvargDest->uiVal = (USHORT) atol(CW2AEX(pvarSrc->bstrVal).c_str());
            }
            pvargDest->vt = vt;
        }
        break;
        case VT_I4:
        case VT_INT:
        {
            if (!pvarSrc->bstrVal || wcslen(pvarSrc->bstrVal) == 0)
            {
                pvargDest->lVal = 0;
            }
            else
            {
                pvargDest->lVal = (LONG) atol(CW2AEX(pvarSrc->bstrVal).c_str());
            }
            pvargDest->vt = VT_I4;
        }
        break;
        case VT_UI4:
        case VT_UINT:
        {
            if (!pvarSrc->bstrVal || wcslen(pvarSrc->bstrVal) == 0)
            {
                pvargDest->ulVal = 0;
            }
            else
            {
                pvargDest->ulVal = (ULONG) atol(CW2AEX(pvarSrc->bstrVal).c_str());
            }
            pvargDest->vt = VT_UI4;
        }
        break;
        case VT_I8:
        {
            if (!pvarSrc->bstrVal || wcslen(pvarSrc->bstrVal) == 0)
            {
                pvargDest->llVal = 0;
            }
            else
            {
                pvargDest->llVal = atoll(CW2AEX(pvarSrc->bstrVal).c_str());
            }
            pvargDest->vt = vt;
        }
        break;
        case VT_UI8:
        {
            if (!pvarSrc->bstrVal || wcslen(pvarSrc->bstrVal) == 0)
            {
                pvargDest->ullVal = 0;
            }
            else
            {
                pvargDest->ullVal = atoll(CW2AEX(pvarSrc->bstrVal).c_str());
            }
            pvargDest->vt = vt;
        }
        break;
        case VT_R4:
        {
            if (!pvarSrc->bstrVal || wcslen(pvarSrc->bstrVal) == 0)
            {
                pvargDest->fltVal = 0;
            }
            else
            {
                pvargDest->fltVal = (FLOAT) atof(CW2AEX(pvarSrc->bstrVal).c_str());
            }
            pvargDest->vt = vt;
        }
        break;
        case VT_R8:
        {
            if (!pvarSrc->bstrVal || wcslen(pvarSrc->bstrVal) == 0)
            {
                pvargDest->dblVal = 0;
            }
            else
            {
                pvargDest->dblVal = atof(CW2AEX(pvarSrc->bstrVal).c_str());
            }
            pvargDest->vt = vt;
        }
        break;
        case VT_BSTR:
        {
            pvargDest->bstrVal = SysAllocString(pvarSrc->bstrVal);
            pvargDest->vt = vt;
        }
        break;

        default:
        {
            assert(false);
            return E_INVALIDARG;
        }
        break;
        }

        if (toFree)
        {
            SysFreeString(toFree);
        }
    }
    break;

    default:
    {
        assert(false);
        return E_INVALIDARG;
    }
    break;

    }
    return S_OK;
}

bool GetComputerNameA(char* lpBuffer, uint32_t* nSize)
{
    assert(nSize);
    memset(lpBuffer, 0, *nSize);
    bool result = 0 == gethostname(lpBuffer, *nSize);

    if (result)
    {
        *nSize = static_cast<uint32_t>(strlen(lpBuffer));
    }
    return result;
}

bool DeleteFileA(const char* lpFileName)
{
    return 0 == remove(lpFileName);
}

bool MoveFileA(const char* lpExistingFileName, const char* lpNewFileName)
{
    return 0 == rename(lpExistingFileName, lpNewFileName);
}

#endif // _WIN32

int Variant::VarTypePriority(VARTYPE vt) noexcept
{
    switch (vt)
    {
    case VT_EMPTY:
    case VT_NULL:
        return 0;
    case VT_BOOL:
        return 1;
    case VT_I1:
    case VT_UI1:
        return 8;
    case VT_I2:
    case VT_UI2:
        return 16;
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
        return 32;
    case VT_I8:
    case VT_UI8:
        return 64;
    case VT_R4:
        return 96;
    case VT_R8:
    case VT_DATE:
        return 128;
    }
    return -1;
}

HRESULT Variant::VarCmp(PCVARIANT pvarLeft, PCVARIANT pvarRight, LCID lcid, ULONG dwFlags) noexcept
{
    assert(pvarLeft);
    assert(pvarRight);

    if (pvarLeft->vt != pvarRight->vt)
    {
        if (pvarLeft->vt == VT_EMPTY || pvarLeft->vt == VT_NULL)
        {
            return VARCMP_LT;
        }
        if (pvarRight->vt == VT_EMPTY || pvarRight->vt == VT_NULL)
        {
            return VARCMP_GT;
        }
        if (pvarLeft->vt == VT_UNKNOWN || pvarLeft->vt == VT_DISPATCH)
        {
            return E_FAIL;
        }
        if (pvarRight->vt == VT_UNKNOWN || pvarRight->vt == VT_DISPATCH)
        {
            return E_FAIL;
        }
        if (pvarLeft->vt == VT_BOOL)
        {
            Variant var(*pvarRight);

            if (SUCCEEDED(var.ChangeType(VT_BOOL)))
            {
                return VarCmp(pvarLeft, &var, lcid, dwFlags);
            }
            return E_FAIL;
        }
        if (pvarRight->vt == VT_BOOL)
        {
            Variant var(*pvarLeft);

            if (SUCCEEDED(var.ChangeType(VT_BOOL)))
            {
                return VarCmp(&var, pvarRight, lcid, dwFlags);
            }
            return E_FAIL;
        }
        if (pvarRight->vt == VT_BSTR || (VarTypePriority(pvarLeft->vt) >= VarTypePriority(pvarRight->vt)))
        {
            Variant var(*pvarRight);

            if (SUCCEEDED(var.ChangeType(pvarLeft->vt)))
            {
                return VarCmp(pvarLeft, &var, lcid, dwFlags);
            }
            return E_FAIL;            
        }
        Variant var(*pvarLeft);

        if (SUCCEEDED(var.ChangeType(pvarRight->vt)))
        {
            return VarCmp(&var, pvarRight, lcid, dwFlags);
        }
        return E_FAIL;
    }

    assert(pvarLeft->vt == pvarRight->vt);

    switch (pvarLeft->vt)
    {
    case VT_EMPTY:
    case VT_NULL:
        return VARCMP_EQ;

    case VT_BOOL:
        if (pvarLeft->boolVal && pvarRight->boolVal)
        {
            return VARCMP_EQ;
        }
        if (!pvarLeft->boolVal && !pvarRight->boolVal)
        {
            return VARCMP_EQ;
        }
        return pvarLeft->boolVal > pvarRight->boolVal ? VARCMP_GT : VARCMP_LT;

    case VT_I1:
        if (pvarLeft->cVal == pvarRight->cVal)
        {
            return VARCMP_EQ;
        }
        return pvarLeft->cVal > pvarRight->cVal ? VARCMP_GT : VARCMP_LT;
    
    case VT_UI1:
        if (pvarLeft->bVal == pvarRight->bVal)
        {
            return VARCMP_EQ;
        }
        return pvarLeft->bVal > pvarRight->bVal ? VARCMP_GT : VARCMP_LT;
    
    case VT_I2:
        if (pvarLeft->iVal == pvarRight->iVal)
        {
            return VARCMP_EQ;
        }
        return pvarLeft->iVal > pvarRight->iVal ? VARCMP_GT : VARCMP_LT;
    
    case VT_UI2:
        if (pvarLeft->uiVal == pvarRight->uiVal)
        {
            return VARCMP_EQ;
        }
        return pvarLeft->uiVal > pvarRight->uiVal ? VARCMP_GT : VARCMP_LT;

    case VT_INT:
    case VT_I4:
        if (pvarLeft->lVal == pvarRight->lVal)
        {
            return VARCMP_EQ;
        }
        return pvarLeft->lVal > pvarRight->lVal ? VARCMP_GT : VARCMP_LT;

    case VT_UINT:
    case VT_UI4:
        if (pvarLeft->ulVal == pvarRight->ulVal)
        {
            return VARCMP_EQ;
        }
        return pvarLeft->ulVal > pvarRight->ulVal ? VARCMP_GT : VARCMP_LT;

    case VT_I8:
        if (pvarLeft->llVal == pvarRight->llVal)
        {
            return VARCMP_EQ;
        }
        return pvarLeft->llVal > pvarRight->llVal ? VARCMP_GT : VARCMP_LT;

    case VT_UI8:
        if (pvarLeft->ullVal == pvarRight->ullVal)
        {
            return VARCMP_EQ;
        }
        return pvarLeft->ullVal > pvarRight->ullVal ? VARCMP_GT : VARCMP_LT;

    case VT_R4:
        if (pvarLeft->fltVal == pvarRight->fltVal)
        {
            return VARCMP_EQ;
        }
        return pvarLeft->fltVal > pvarRight->fltVal ? VARCMP_GT : VARCMP_LT;

    case VT_DATE:
    case VT_R8:
        if (pvarLeft->dblVal == pvarRight->dblVal)
        {
            return VARCMP_EQ;
        }
        return pvarLeft->dblVal > pvarRight->dblVal ? VARCMP_GT : VARCMP_LT;

    case VT_BSTR:
    {
        if (pvarLeft->bstrVal == pvarRight->bstrVal)
        {
            return VARCMP_EQ;
        }
        if (pvarLeft->bstrVal == NULL)
        {
            return VARCMP_LT;
        }
        if (pvarRight->bstrVal == NULL)
        {
            return VARCMP_GT;
        }
        int n = wcscmp(pvarLeft->bstrVal, pvarRight->bstrVal);

        if (n == 0)
        {
            return VARCMP_EQ;
        }
        return n > 0 ? VARCMP_GT : VARCMP_LT;
    }
    break;

    default:
        assert(false);
        break;
    }
    return E_FAIL;
}

Variant& Variant::operator=(const std::vector<uint8_t>& src)
{
    Clear();
    punkVal = new BlobStream(src);
    punkVal->AddRef();
    vt = VT_UNKNOWN;
    return *this;
}

Variant& Variant::operator=(std::vector<uint8_t>&& src)
{
    Clear();
    punkVal = new BlobStream(std::move(src));
    punkVal->AddRef();
    vt = VT_UNKNOWN;
    return *this;
}

bool FromJson(IValueCollection* valueCollection, IStream* jsonStream)
{
    if (!valueCollection || !jsonStream)
    {
        return false;
    }
    jsonStream->Seek({}, STREAM_SEEK_SET, nullptr);

    class Handler
    {
    private:

        enum class ObjectType
        {
            valueCollection,
            stream
        };

        class KeyID
				: public ICollectionKey
        {
        public:
            KeyID(const char* name, size_t length)
                : m_name{ name ? name : "", length }
            {
                m_id = std::hash<std::string>{}(m_name);
            }

            KeyID(const KeyID& i)
                : m_name{ i.m_name }
                , m_id{ i.m_id }
            {
            }

            KeyID(KeyID&& i) noexcept
                : m_name{ move(i.m_name) }
                , m_id{ i.m_id }
            {
                i.m_id = 0;
            }

            KeyID() noexcept
                : m_name{}
                , m_id{ 0 }
            {
            }

            KeyID& operator=(const KeyID& i)
            {
                m_name = i.m_name;
                m_id = i.m_id;
                return *this;
            }

            KeyID& operator=(KeyID&& i) noexcept
            {
                m_name = move(i.m_name);
                m_id = i.m_id;
                i.m_id = 0;
                return *this;
            }

            const char* Name() const noexcept override
            {
                return m_name.c_str();
            }

            size_t Id() const noexcept override
            {
                return m_id;
            }

            void Done() noexcept
            {
                if (!m_name.empty())
                {
                    m_name.clear();
                    m_id = 0;
                }
                else
                {
                    ++m_id;
                }
            }

        private:
            size_t m_id;
            std::string m_name;
        };
        KeyID m_currentKey;
        using Object = std::pair<SharedPtr<IUnknown>, ObjectType>;
        using StackItem = std::pair<KeyID, Object>;
        std::stack<StackItem> m_stack;
        SharedPtr<IValueCollection> m_currentValueCollection;

    private:
        bool CurrentObjectIsStream() const noexcept
        {
            if (!m_stack.empty())
            {
                return m_stack.top().second.second == ObjectType::stream;
            }
            return false;
        }

        SharedPtr<IValueCollection> GetCurrentValueCollection() const
        {
            if (!m_stack.empty())
            {
                SharedPtr<IValueCollection> result;
                m_stack.top().second.first->QueryInterface(IID_IValueCollection, (void**)&result.p);
                return result;
            }
            return m_currentValueCollection;
        }

        SharedPtr<IStream> GetCurrentStream() const
        {
            assert(CurrentObjectIsStream());
            SharedPtr<IStream> result;
            m_stack.top().second.first->QueryInterface(IID_IStream, (void**)&result.p);
            return result;
        }

        void Reduce()
        {
            assert(!m_stack.empty());

            auto reduce = move(m_stack.top());
            m_stack.pop();

            m_currentKey = std::move(reduce.first);

            if (reduce.second.second == ObjectType::valueCollection)
            {
                SharedPtr<IValueCollection> valueCollection;
                reduce.second.first->QueryInterface(IID_IValueCollection, (void**)&valueCollection.p);

                if (!m_stack.empty())
                {
                    assert(!CurrentObjectIsStream());
                    GetCurrentValueCollection()->Set(m_currentKey, valueCollection.p);
                }
                else
                {
                    m_currentValueCollection = std::move(valueCollection);
                }
            }
            else
            {
                assert(reduce.second.second == ObjectType::stream);

                SharedPtr<IStream> stream; 
                reduce.second.first->QueryInterface(IID_IStream, (void**)&stream.p);
                stream->Seek({}, STREAM_SEEK_SET, nullptr);

                assert(!CurrentObjectIsStream());
                GetCurrentValueCollection()->Set(m_currentKey, stream.p);
            }
            m_currentKey.Done();
        }

        void ConvertStreamToValues()
        {
            assert(!m_stack.empty());
            assert(m_stack.top().second.second == ObjectType::stream);

            SharedPtr<IStream> stream(std::move(m_stack.top().second.first));
            assert(stream.IsValid());
            assert(!m_stack.top().second.first.IsValid());

            size_t streamSize = BlobStream::GetSize(stream);

            KeyID key;
            auto valueCollection = std::move(m_currentValueCollection);

            if (!valueCollection.IsValid())
            {
                valueCollection = MakeShared<ValueCollection>();
            }
            if (streamSize)
            {
                stream->Seek({}, STREAM_SEEK_SET, nullptr);

                for (size_t i = 0; i < streamSize; ++i)
                {
                    BYTE byte = 0;

                    if (S_OK != stream->Read(&byte, 1, nullptr))
                    {
                        assert(false);
                    }
                    valueCollection->Set(key, byte);
                    key.Done();
                }
            }
            m_stack.top().second.first = std::move(valueCollection);
            assert(!valueCollection.IsValid());
            assert(m_stack.top().second.first.IsValid());
            m_stack.top().second.second = ObjectType::valueCollection;

            m_currentKey = std::move(key);
        }

    public:
        Handler(IValueCollection* valueCollection) noexcept
            : m_currentValueCollection(valueCollection)
        {
        }

        bool Null()
        {
            if (CurrentObjectIsStream())
            {
                ConvertStreamToValues();
            }
            Variant varNull;
            varNull.vt = VT_NULL;

            GetCurrentValueCollection()->Set(m_currentKey, std::move(varNull));
            m_currentKey.Done();
            return true;
        }

        bool Bool(bool val)
        {
            if (CurrentObjectIsStream())
            {
                ConvertStreamToValues();
            }
            Variant varBool;
            varBool.boolVal = val ? VARIANT_TRUE : VARIANT_FALSE;
            varBool.vt = VT_BOOL;

            GetCurrentValueCollection()->Set(m_currentKey, std::move(varBool));
            m_currentKey.Done();
            return true;
        }

        bool Int(int val)
        {
            if (CurrentObjectIsStream())
            {
                if (val < 0 || val > 255)
                {
                    ConvertStreamToValues();
                }
                else
                {
                    const BYTE byte = static_cast<BYTE>(val);
                    GetCurrentStream()->Write(&byte, 1, nullptr);
                    return true;
                }
            }
            Variant varInt;
            varInt.lVal = val;
            varInt.vt = VT_I4;

            GetCurrentValueCollection()->Set(m_currentKey, std::move(varInt));
            m_currentKey.Done();
            return true;
        }

        bool Uint(unsigned val)
        {
            if (CurrentObjectIsStream())
            {
                if (val > 255)
                {
                    ConvertStreamToValues();
                }
                else
                {
                    const BYTE byte = static_cast<BYTE>(val);
                    GetCurrentStream()->Write(&byte, 1, nullptr);
                    return true;
                }
            }
            Variant varUInt;
            varUInt.ulVal = val;
            varUInt.vt = VT_UI4;

            GetCurrentValueCollection()->Set(m_currentKey, std::move(varUInt));
            m_currentKey.Done();
            return true;
        }

        bool Int64(int64_t val)
        {
            if (CurrentObjectIsStream())
            {
                if (val < 0 || val > 255)
                {
                    ConvertStreamToValues();
                }
                else
                {
                    const BYTE byte = static_cast<BYTE>(val);
                    GetCurrentStream()->Write(&byte, 1, nullptr);
                    return true;
                }
            }
            Variant varInt64;
            varInt64.llVal = val;
            varInt64.vt = VT_I8;

            GetCurrentValueCollection()->Set(m_currentKey, std::move(varInt64));
            m_currentKey.Done();
            return true;
        }

        bool Uint64(uint64_t val)
        {
            if (CurrentObjectIsStream())
            {
                if (val > 255)
                {
                    ConvertStreamToValues();
                }
                else
                {
                    const BYTE byte = static_cast<BYTE>(val);
                    GetCurrentStream()->Write(&byte, 1, nullptr);
                    return true;
                }
            }
            Variant varUInt64;
            varUInt64.ullVal = val;
            varUInt64.vt = VT_UI8;

            GetCurrentValueCollection()->Set(m_currentKey, std::move(varUInt64));
            m_currentKey.Done();
            return true;
        }

        bool Double(double val)
        {
            if (CurrentObjectIsStream())
            {
                ConvertStreamToValues();
            }
            Variant varDouble;
            varDouble.dblVal = val;
            varDouble.vt = VT_R8;
            
            GetCurrentValueCollection()->Set(m_currentKey, std::move(varDouble));
            m_currentKey.Done();
            return true;
        }

        bool RawNumber(const char*, rapidjson::SizeType, bool)
        {
            assert(false);
            return false;
        }

        bool String(const char* str, rapidjson::SizeType length, bool)
        {
            if (CurrentObjectIsStream())
            {
                ConvertStreamToValues();
            }
            std::string val;

            if (str && length)
            {
                val = std::string(str, length);
            }
            GetCurrentValueCollection()->Set(m_currentKey, val);
            m_currentKey.Done();
            return true;
        }

        bool Key(const char* str, rapidjson::SizeType length, bool)
        {
            m_currentKey = KeyID(str, static_cast<size_t>(length));
            return true;
        }

        bool StartObject()
        {
            if (CurrentObjectIsStream())
            {
                ConvertStreamToValues();
            }
            if (!m_currentValueCollection)
            {
                m_currentValueCollection = MakeShared<ValueCollection>();
            }
            auto currentValueCollection = std::move(m_currentValueCollection);
            m_stack.emplace(StackItem{ std::move(m_currentKey), Object{ std::move(currentValueCollection), ObjectType::valueCollection } });
            assert(!m_currentValueCollection.IsValid());
            return true;
        }

        bool EndObject(rapidjson::SizeType)
        {
            Reduce();
            return true;
        }

        bool StartArray()
        {
            if (CurrentObjectIsStream())
            {
                ConvertStreamToValues();
            }
            auto stream = MakeShared<BlobStream>();

            m_stack.emplace(StackItem{ std::move(m_currentKey), Object{ std::move(stream), ObjectType::stream } });
            return true;
        }

        bool EndArray(rapidjson::SizeType)
        {
            Reduce();
            return true;
        }
    };

    class StreamWrapper
    {
    public:
        StreamWrapper(IStream* stream) noexcept
            : m_stream(stream)
        {
        }

        typedef char Ch;

        StreamWrapper(const StreamWrapper&) = delete;
        StreamWrapper& operator=(const StreamWrapper&) = delete;

        Ch Peek() const noexcept
        {
            if (m_chIsValid)
            {
                return m_ch;
            }
            ULONG read = 0;

            if (S_OK == m_stream->Read(&m_ch, sizeof(m_ch), &read) && static_cast<size_t>(read) == sizeof(m_ch))
            {
                m_chIsValid = true;
            }
            else
            {
                m_ch = 0;
            }
            return m_ch;
        }

        Ch Take() noexcept
        {
            if (!m_chIsValid)
            {
                Peek();

                if (m_chIsValid)
                {
                    ++m_tell;
                }
            }
            else
            {
                ++m_tell;
            }
            m_chIsValid = false;
            return m_ch;
        }

        size_t Tell() const noexcept
        {
            return m_tell;
        }

        Ch* PutBegin() noexcept
        {
            assert(false);
            return nullptr;
        }

        void Put(Ch) noexcept
        {
            assert(false);
        }

        void Flush() noexcept
        {
            assert(false);
        }

        size_t PutEnd(Ch*) noexcept
        {
            assert(false);
            return 0;
        }

    private:
        size_t m_tell{ 0 };
        mutable bool m_chIsValid{ false };
        mutable Ch m_ch{ 0 };
        IStream* const m_stream;
    };

    StreamWrapper streamWrapper(jsonStream);
    Handler handler(valueCollection);

    rapidjson::Reader reader;
    
    return !reader.Parse(streamWrapper, handler).IsError();
}

static rapidjson::Value MakeRapidJsonValue(rapidjson::Document& document, const Variant& value)
{
    rapidjson::Value result;

    switch (value.vt)
    {
    case VT_NULL:
    {
        result.SetNull();
    }
    break;

    case VT_BSTR:
    {
        result.SetString(CW2AEX(value.bstrVal), document.GetAllocator());
    }
    break;

    case VT_BOOL:
    {
        result.SetBool(value.boolVal != VARIANT_FALSE ? true : false);
    }
    break;

    case VT_R4:
    {
        result.SetDouble(value.fltVal);
    }
    break;

    case VT_R8:
    {
        result.SetDouble(value.dblVal);
    }
    break;

    case VT_I1:
    {
        result.SetInt64(value.cVal);
    }
    break;

    case VT_I2:
    {
        result.SetInt64(value.iVal);
    }
    break;

    case VT_I4:
    {
        result.SetInt64(value.lVal);
    }
    break;

    case VT_I8:
    {
        result.SetInt64(value.llVal);
    }
    break;

     case VT_UI1:
    {
        result.SetInt64(value.bVal);
    }
    break;

    case VT_UI2:
    {
        result.SetInt64(value.uiVal);
    }
    break;

    case VT_UI4:
    {
        result.SetInt64(value.ulVal);
    }
    break;

    case VT_UI8:
    {
        result.SetInt64(value.ullVal);
    }
    break;

    case VT_UNKNOWN:
    {
        SharedPtr<IStream> stream;

        if (value.punkVal && SUCCEEDED(value.punkVal->QueryInterface(IID_IStream, (void**)&stream.p)))
        {
            result.SetArray();

            const size_t size = BlobStream::GetSize(stream);

            if (size)
            {
                stream->Seek({}, STREAM_SEEK_SET, nullptr);
                rapidjson::Value arrayValue;

                for (size_t i = 0; i < size; ++i)
                {
                    BYTE byte = 0;

                    if (S_OK != stream->Read(&byte, 1, nullptr))
                    {
                        break;
                    }
                    arrayValue.SetUint(static_cast<unsigned int>(byte));
                    result.PushBack(arrayValue, document.GetAllocator());
                }
            }
        }
        else
        {
            assert(false);
        }
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

static void ToJson(const std::string& root, rapidjson::Document& document, const IValueCollection* valueCollection)
{
    if (valueCollection)
    {
        const size_t count = valueCollection->Count();
        bool handled = false;

        for (size_t i = 0; i < count; ++i)
        {
            Variant key;
            Variant value = valueCollection->ByIndex(i, &key);

            if (key.vt != VT_EMPTY)
            {
                if (value.vt == VT_UNKNOWN)
                {
                    SharedPtr<IStream> stream;

                    if (SUCCEEDED(value.punkVal->QueryInterface(IID_IStream, (void**)&stream.p)))
                    {
                        const std::string pointer = root + VariantValue::Get<std::string>(key);
                        rapidjson::Pointer(pointer.c_str()).Set(document, MakeRapidJsonValue(document, value));
                        handled = true;
                    }
                    else
                    {
                        SharedPtr<IValueCollection> object; 

                        if (SUCCEEDED(value.punkVal->QueryInterface(IID_IValueCollection, (void**)&object.p)))
                        {
                            const std::string pointer = root + VariantValue::Get<std::string>(key) + "/"s;
                            ToJson(pointer, document, object.p);
                            handled = true;
                        }
                    }
                }
                else
                {
                    const std::string pointer = root + VariantValue::Get<std::string>(key);
                    rapidjson::Pointer(pointer.c_str()).Set(document, MakeRapidJsonValue(document, value));
                    handled = true;
                }
            }
            else
            {
                break;
            }
        }
                
        if (!handled)
        {
            assert(!root.empty());
            assert(*root.rbegin() == '/');

            const rapidjson::Value emptyObject(rapidjson::kObjectType);

            rapidjson::Pointer(std::string{ root.substr(0, root.length()-1) }).Set(document, emptyObject);
            return;
        }
    }
}

SharedPtr<IStream> ToJson(const IValueCollection* valueCollection, JsonFormat format /*= JsonFormat::compact*/)
{
    SharedPtr<IStream> result = new BlobStream;
    
    if (!ToJson(valueCollection, result.p, format))
    {
        std::runtime_error("failed to create json");
    }
    result->Seek({}, STREAM_SEEK_SET, nullptr);
    return result;
}

bool ToJson(const IValueCollection* valueCollection, IStream* stream, JsonFormat format /*= JsonFormat::compact*/)
{
    if (!valueCollection || !stream)
    {
        return false;
    }

    rapidjson::Document document;

    ToJson("/"s, document, valueCollection);

    class StreamWrapper
    {
    public:
        typedef char Ch;

        StreamWrapper(IStream* stream) noexcept
            : m_stream(stream)
            , size(0)
        {
            assert(m_stream);
        }

        Ch Peek() const noexcept
        {
            assert(false);
            return '\0';
        }

        Ch Take() noexcept
        {
            assert(false);
            return '\0';
        }

        size_t Tell() const noexcept
        {
            return size;
        }

        Ch* PutBegin() noexcept
        {
            assert(false);
            return 0;
        }

        void Put(Ch c)
        {
            if (S_OK == m_stream->Write(&c, 1, nullptr))
            {
                ++size;
            }
            else
            {
                throw(std::runtime_error("failed to write to stream"));
            }
        }

        void Flush() noexcept
        {
        }

        size_t PutEnd(Ch*) noexcept
        {
            assert(false);
            return 0;
        }

    private:
        IStream* const m_stream;
        size_t size;
    };

    StreamWrapper buffer(stream);

    if (format == JsonFormat::compact)
    {
        rapidjson::Writer<StreamWrapper> writer(buffer);

        if (!document.Accept(writer))
        {
            return false;
        }
    }
    else
    {
        rapidjson::PrettyWriter<StreamWrapper> prettyWriter(buffer);

        if (!document.Accept(prettyWriter))
        {
            return false;
        }
    }
    return true;
}