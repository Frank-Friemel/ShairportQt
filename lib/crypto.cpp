#include <string>
#include "base64.h"
#include "crypto.h"
#include <vector>
#include <stdexcept>

#ifdef _WIN32

#include "LayerCake.h"
#include <bcrypt.h>

#pragma comment(lib, "Bcrypt.lib")

#else

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/aes.h>

#endif

namespace Crypto
{
#ifdef _WIN32

static const std::string superSecretKey =   
                "UlNBMwAIAAADAAAAAAEAAIAAAACAAAAAAQAB59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U"
                "3GhC/j0Qg90u3sG/1CUtwC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ"
                "2YCCLlDRKSKv6kDqnw4UwPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuBOitnZ/"
                "bDzPHrTOZz0Dew0uowxf/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJQ+87X6oV3eaYvt3zWZYD"
                "6z5vYTcrtij2VZ9Zmni/UAaHqn9JdsBWLUEpVviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEe/fg"
                "v1oeZxgxmotiCcMXFEQEWflzhWYTsXrhUIuz5jFua39GLS99ZEErhLdrwj8rDDViRVJ5skOp9zFv"
                "lYAHs0xh92ji1E7V/ysnKBfsMrPkk5KSKPrnjndMoPdevWnVkgJ5jxFuNgxkOLMuG9i53B4yMvDT"
                "CRiIPMQ++N2iLDaR72//+ZTx5WRBqgA1/RmgyNbwI3jHBYDZxIQgeR30B8WR+26/yjIsMIbdkB/S"
                "+uGuu2St9rt5/4BRvr0M2CCriYdABgGnsv6TkMrMmsq47Sv5HRhtj2lkPX7+D11W33V3otA16lQT"
                "/JjY8/kI2gWaN52kscw48V1WCoPMMXFTyEvQ66+8QCW6gYx1cCM0OE6PaW+ATXqg53ZOUHu309/v"
                "x9Z4xmgtP61xNEG+6uckoJ7Am9w7wHCckTPUiezipRrdBTEnSQ+ShtFzyKQFTcIKV1x+TAyYNPSh"
                "3odJF6PkAOr4hQYttct+NDaJ5xH3X+eD1+GRkv12nNVCvqS5AQfs0X9AGNx96iktpTBCOG8xBaB3"
                "itxvPeaQ2it0xQVZg+31dGYaL9e33oBTzMDiCPDIrGJvWX09mdLOUaN7Oa5Lfp7ywHXwvz2Dys0y"
                "2paRksKJkjWCXAfRzTJZoZBs3NSZy2E+IslMseqXGQZgnfGw9IsGPxc3IDQ2lJm1/flw70QNkE7p"
                "IPlE71qvfJQgoA9em0gILAuE4Pu13aKiJnfft7hIjbK+5kyb3TysZvoyDnb3HOKvInK7vXbKuU4I"
                "SgxB2bB3HcYzQMGsz1qJ2gG0N5hvJpzwwhbhXqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjo"
                "D2QYjhBGuhvkWKbl8Axy9XfWBLmkzkEiqoSwF0PsmVrPzH9KsnwLGH+QZlvjWd8SWYGN7u1507Hv"
                "hF5N3drJoVU3O14nDY4TFQAaLlJ9VM35AApXaLyY1ERrN7u9ALKd2LUwYhM7Km539O4yUFYikE2n"
                "IPscEsA5ltpxOgUGCY7b7ez5NtD6nL1ZKauw7aNXmVAvmJTcuPxWmoktF3gDJKK2wxZuNGcJE0uF"
                "QEG4Z3BrWP7yoNuSK3dii2jmlpPHr0O/KnPQtzI3eguhe0TwUem/eYSdyzMyVx/YpwkzwtYL3sR5"
                "k0o9rKQLtvLzfAqdBxBurcizaaA/L0HIgAmOit1GJA2saMxTVPNh";

class RsaInternal
{
public:
    typedef enum
    {
        encrypt_oaep,
        decrypt_oaep,
        encrypt_pkcs1,
        decrypt_pkcs1
    } cryptMode;

public:
    RsaInternal()
    {
        m_hCrypt = NULL;
        m_hKey = NULL;

        if (0 != ::BCryptOpenAlgorithmProvider(&m_hCrypt, BCRYPT_RSA_ALGORITHM, NULL, 0))
        {
            throw std::runtime_error("failed to BCryptOpenAlgorithmProvider");
        }
        const auto key = Base64::Decode(superSecretKey);
        
        const BCRYPT_RSAKEY_BLOB* pKeyBlob = (const BCRYPT_RSAKEY_BLOB*)key.data();

        switch (pKeyBlob->Magic)
        {
        case BCRYPT_RSAFULLPRIVATE_MAGIC:
        {
            if (0 != ::BCryptImportKeyPair(m_hCrypt, NULL, BCRYPT_RSAFULLPRIVATE_BLOB, &m_hKey, (PUCHAR)pKeyBlob, static_cast<ULONG>(key.size()), 0))
            {
                ::BCryptCloseAlgorithmProvider(m_hCrypt, 0);
                throw std::runtime_error("failed to BCryptImportKeyPair");
            }
        }
        break;

        case BCRYPT_RSAPRIVATE_MAGIC:
        {
            if (0 != ::BCryptImportKeyPair(m_hCrypt, NULL, BCRYPT_RSAPRIVATE_BLOB, &m_hKey, (PUCHAR)pKeyBlob, static_cast<ULONG>(key.size()), 0))
            {
                ::BCryptCloseAlgorithmProvider(m_hCrypt, 0);
                throw std::runtime_error("failed to BCryptImportKeyPair");
            }
        }
        break;

        case BCRYPT_RSAPUBLIC_MAGIC:
        {
            if (0 != ::BCryptImportKeyPair(m_hCrypt, NULL, BCRYPT_RSAPUBLIC_BLOB, &m_hKey, (PUCHAR)pKeyBlob, static_cast<ULONG>(key.size()), 0))
            {
                ::BCryptCloseAlgorithmProvider(m_hCrypt, 0);
                throw std::runtime_error("failed to BCryptImportKeyPair");
            }
        }
        break;
        }
    }
    ~RsaInternal()
    {
        if (m_hKey)
        {
            ::BCryptDestroyKey(m_hKey);
            m_hKey = NULL;
        }

        if (m_hCrypt)
        {
            ::BCryptCloseAlgorithmProvider(m_hCrypt, 0);
        }
    }
    
    bool Crypt(cryptMode nMode, const uint8_t* pbDataIn, ULONG cbDataIn, std::vector<uint8_t>& blobOut, PCWSTR strPaddingHashId = BCRYPT_SHA512_ALGORITHM)
    {
        if (cbDataIn == 0)
        {
            blobOut.clear();
            return true;
        }

        bool r = false;

        ULONG cbOutput = 0;

        BCRYPT_OAEP_PADDING_INFO oaep_paddingInfo;

        memset(&oaep_paddingInfo, 0, sizeof(oaep_paddingInfo));
        oaep_paddingInfo.pszAlgId = strPaddingHashId;

        switch (nMode)
        {
        case encrypt_oaep:
        {
            r = 0 == ::BCryptEncrypt(m_hKey, (PUCHAR)pbDataIn, cbDataIn, &oaep_paddingInfo, NULL, 0, NULL, 0, &cbOutput, BCRYPT_PAD_OAEP);
        }
        break;

        case decrypt_oaep:
        {
            r = 0 == ::BCryptDecrypt(m_hKey, (PUCHAR)pbDataIn, cbDataIn, &oaep_paddingInfo, NULL, 0, NULL, 0, &cbOutput, BCRYPT_PAD_OAEP);
        }
        break;

        case encrypt_pkcs1:
        {
            r = 0 == ::BCryptEncrypt(m_hKey, (PUCHAR)pbDataIn, cbDataIn, NULL, NULL, 0, NULL, 0, &cbOutput, BCRYPT_PAD_PKCS1);
        }
        break;

        case decrypt_pkcs1:
        {
            r = 0 == ::BCryptDecrypt(m_hKey, (PUCHAR)pbDataIn, cbDataIn, NULL, NULL, 0, NULL, 0, &cbOutput, BCRYPT_PAD_PKCS1);
        }
        break;
        }
        if (!r)
        {
            return false;
        }
        blobOut.resize(cbOutput);

        switch (nMode)
        {
        case encrypt_oaep:
        {
            r = 0 == ::BCryptEncrypt(m_hKey, (PUCHAR)pbDataIn, cbDataIn, &oaep_paddingInfo, NULL, 0, blobOut.data(), cbOutput, &cbOutput, BCRYPT_PAD_OAEP);
        }
        break;

        case decrypt_oaep:
        {
            r = 0 == ::BCryptDecrypt(m_hKey, (PUCHAR)pbDataIn, cbDataIn, &oaep_paddingInfo, NULL, 0, blobOut.data(), cbOutput, &cbOutput, BCRYPT_PAD_OAEP);
        }
        break;

        case encrypt_pkcs1:
        {
            r = 0 == ::BCryptEncrypt(m_hKey, (PUCHAR)pbDataIn, cbDataIn, NULL, NULL, 0, blobOut.data(), cbOutput, &cbOutput, BCRYPT_PAD_PKCS1);
        }
        break;

        case decrypt_pkcs1:
        {
            r = 0 == ::BCryptDecrypt(m_hKey, (PUCHAR)pbDataIn, cbDataIn, NULL, NULL, 0, blobOut.data(), cbOutput, &cbOutput, BCRYPT_PAD_PKCS1);
        }
        break;
        }
        return r;
    }

    bool Sign(PCWSTR strHashId, const uint8_t* pbDataIn, ULONG cbDataIn, std::vector<uint8_t>& blobOut)
    {
        BCRYPT_PKCS1_PADDING_INFO pkcs1_paddingInfo;

        memset(&pkcs1_paddingInfo, 0, sizeof(pkcs1_paddingInfo));
        pkcs1_paddingInfo.pszAlgId = strHashId;

        ULONG cbOutput = 0;

        if (0 != ::BCryptSignHash(m_hKey, &pkcs1_paddingInfo, (PUCHAR)pbDataIn, cbDataIn, NULL, 0, &cbOutput, BCRYPT_PAD_PKCS1))
        {
            return false;
        }
        blobOut.resize(cbOutput);

        if (0 != ::BCryptSignHash(m_hKey, &pkcs1_paddingInfo, (PUCHAR)pbDataIn, cbDataIn, blobOut.data(), cbOutput, &cbOutput, BCRYPT_PAD_PKCS1))
        {
            return false;
        }
        return true;
    }

public:
    BCRYPT_ALG_HANDLE m_hCrypt;
    BCRYPT_KEY_HANDLE m_hKey;
};

Rsa::Rsa()
{
    m_handle = new RsaInternal;
}

Rsa::~Rsa()
{
    delete (RsaInternal*)m_handle;
}

std::vector<uint8_t> Rsa::Sign(const std::vector<uint8_t>& input) const
{
    const std::lock_guard<std::mutex> guard(m_mtx);

    std::vector<uint8_t> output;
    
    ((RsaInternal*)m_handle)->Sign(NULL, input.data(), static_cast<ULONG>(input.size()), output);

    return output;
}

std::vector<uint8_t> Rsa::Decrypt(const std::vector<uint8_t>& input) const
{
    const std::lock_guard<std::mutex> guard(m_mtx);

    std::vector<uint8_t> output;

    ((RsaInternal*)m_handle)->Crypt(RsaInternal::cryptMode::decrypt_oaep, input.data(), static_cast<ULONG>(input.size()), output, BCRYPT_SHA1_ALGORITHM);

    return output;
}

Aes::Aes(const std::vector<uint8_t>& key)
{
    m_handle = nullptr;

    std::vector<uint8_t> keyBlob;
    keyBlob.resize(sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + key.size());
    memset(keyBlob.data(), 0, sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + key.size());

    BCRYPT_ALG_HANDLE hCrypt = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;

    if (0 != ::BCryptOpenAlgorithmProvider(&hCrypt, BCRYPT_AES_ALGORITHM, NULL, 0))
    {
        throw std::runtime_error("failed to BCryptOpenAlgorithmProvider");
    }
    if (0 != ::BCryptSetProperty(hCrypt, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0))
    {
        ::BCryptCloseAlgorithmProvider(hCrypt, 0);
        throw std::runtime_error("failed to BCryptSetProperty");
    }

    ((BCRYPT_KEY_DATA_BLOB_HEADER*)keyBlob.data())->dwMagic = BCRYPT_KEY_DATA_BLOB_MAGIC;
    ((BCRYPT_KEY_DATA_BLOB_HEADER*)keyBlob.data())->dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
    ((BCRYPT_KEY_DATA_BLOB_HEADER*)keyBlob.data())->cbKeyData = static_cast<ULONG>(key.size());

    memcpy(keyBlob.data() + sizeof(BCRYPT_KEY_DATA_BLOB_HEADER), key.data(), key.size());

    if (0 != ::BCryptImportKey(hCrypt, NULL, BCRYPT_KEY_DATA_BLOB, &hKey, NULL, 0
        , (PUCHAR)(BCRYPT_KEY_DATA_BLOB_HEADER*)keyBlob.data(), static_cast<ULONG>(sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + key.size()), 0))
    {
        ::BCryptCloseAlgorithmProvider(hCrypt, 0);
        throw std::runtime_error("failed to BCryptImportKey");
    }
    m_handle = new std::pair<BCRYPT_ALG_HANDLE, BCRYPT_KEY_HANDLE>(hCrypt, hKey);
}

Aes::~Aes()
{
    std::pair<BCRYPT_ALG_HANDLE, BCRYPT_KEY_HANDLE>* ph = static_cast<std::pair<BCRYPT_ALG_HANDLE, BCRYPT_KEY_HANDLE>*>(m_handle);

    if (ph)
    {
        ::BCryptDestroyKey(ph->second);
        ::BCryptCloseAlgorithmProvider(ph->first, 0);
        delete ph;
    }
}

void Aes::Decrypt(const unsigned char* in, unsigned char* out, size_t size, uint8_t* iv, size_t ivLen)
{
    const std::lock_guard<std::mutex> guard(m_mtx);

    std::pair<BCRYPT_ALG_HANDLE, BCRYPT_KEY_HANDLE>* ph = static_cast<std::pair<BCRYPT_ALG_HANDLE, BCRYPT_KEY_HANDLE>*>(m_handle);

    ULONG r = 0;

    if (0 != ::BCryptDecrypt(ph->second, (PUCHAR)in, static_cast<ULONG>(size), NULL, iv,
                static_cast<ULONG>(ivLen), out, static_cast<ULONG>(size), &r, 0))
    {
        throw std::runtime_error("failed to BCryptDecrypt");                    
    }
}

#else

static const char superSecretKey[] =
                "-----BEGIN RSA PRIVATE KEY-----\n"
                "MIIEpQIBAAKCAQEA59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUt\n"
                "wC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDRKSKv6kDqnw4U\n"
                "wPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuBOitnZ/bDzPHrTOZz0Dew0uowxf\n"
                "/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJQ+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/\n"
                "UAaHqn9JdsBWLUEpVviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEewIDAQABAoIBAQDl8Axy9XfW\n"
                "BLmkzkEiqoSwF0PsmVrPzH9KsnwLGH+QZlvjWd8SWYGN7u1507HvhF5N3drJoVU3O14nDY4TFQAa\n"
                "LlJ9VM35AApXaLyY1ERrN7u9ALKd2LUwYhM7Km539O4yUFYikE2nIPscEsA5ltpxOgUGCY7b7ez5\n"
                "NtD6nL1ZKauw7aNXmVAvmJTcuPxWmoktF3gDJKK2wxZuNGcJE0uFQEG4Z3BrWP7yoNuSK3dii2jm\n"
                "lpPHr0O/KnPQtzI3eguhe0TwUem/eYSdyzMyVx/YpwkzwtYL3sR5k0o9rKQLtvLzfAqdBxBurciz\n"
                "aaA/L0HIgAmOit1GJA2saMxTVPNhAoGBAPfgv1oeZxgxmotiCcMXFEQEWflzhWYTsXrhUIuz5jFu\n"
                "a39GLS99ZEErhLdrwj8rDDViRVJ5skOp9zFvlYAHs0xh92ji1E7V/ysnKBfsMrPkk5KSKPrnjndM\n"
                "oPdevWnVkgJ5jxFuNgxkOLMuG9i53B4yMvDTCRiIPMQ++N2iLDaRAoGBAO9v//mU8eVkQaoANf0Z\n"
                "oMjW8CN4xwWA2cSEIHkd9AfFkftuv8oyLDCG3ZAf0vrhrrtkrfa7ef+AUb69DNggq4mHQAYBp7L+\n"
                "k5DKzJrKuO0r+R0YbY9pZD1+/g9dVt91d6LQNepUE/yY2PP5CNoFmjedpLHMOPFdVgqDzDFxU8hL\n"
                "AoGBANDrr7xAJbqBjHVwIzQ4To9pb4BNeqDndk5Qe7fT3+/H1njGaC0/rXE0Qb7q5ySgnsCb3DvA\n"
                "cJyRM9SJ7OKlGt0FMSdJD5KG0XPIpAVNwgpXXH5MDJg09KHeh0kXo+QA6viFBi21y340NonnEfdf\n"
                "54PX4ZGS/Xac1UK+pLkBB+zRAoGAf0AY3H3qKS2lMEI4bzEFoHeK3G895pDaK3TFBVmD7fV0Zhov\n"
                "17fegFPMwOII8MisYm9ZfT2Z0s5Ro3s5rkt+nvLAdfC/PYPKzTLalpGSwomSNYJcB9HNMlmhkGzc\n"
                "1JnLYT4iyUyx6pcZBmCd8bD0iwY/FzcgNDaUmbX9+XDvRA0CgYEAkE7pIPlE71qvfJQgoA9em0gI\n"
                "LAuE4Pu13aKiJnfft7hIjbK+5kyb3TysZvoyDnb3HOKvInK7vXbKuU4ISgxB2bB3HcYzQMGsz1qJ\n"
                "2gG0N5hvJpzwwhbhXqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjoD2QYjhBGuhvkWKY=\n"
                "-----END RSA PRIVATE KEY-----";

Rsa::Rsa()
{
    BIO* bmem = BIO_new_mem_buf(superSecretKey, -1);

    if (!bmem)
    {
        throw std::bad_alloc();
    }

    m_handle = PEM_read_bio_RSAPrivateKey(bmem, NULL, NULL, NULL);
    BIO_free(bmem);

    if (!m_handle)
    {
        throw std::runtime_error("Failed to read Private-Key");
    }
}

Rsa::~Rsa()
{
    RSA_free((RSA*)m_handle);
}

std::vector<uint8_t> Rsa::Sign(const std::vector<uint8_t>& input) const
{
    const std::lock_guard<std::mutex> guard(m_mtx);

    std::vector<uint8_t> output;
    output.resize(RSA_size((RSA*)m_handle));

    output.resize(RSA_private_encrypt(static_cast<int>(input.size()), input.data(), output.data(), (RSA*)m_handle,
                                        RSA_PKCS1_PADDING));
    return output;
}

std::vector<uint8_t> Rsa::Decrypt(const std::vector<uint8_t>& input) const
{
    const std::lock_guard<std::mutex> guard(m_mtx);

    std::vector<uint8_t> output;
    output.resize(RSA_size((RSA*)m_handle));

    output.resize(RSA_private_decrypt(static_cast<int>(input.size()), input.data(), output.data(), (RSA*)m_handle,
                                        RSA_PKCS1_OAEP_PADDING));
    return output;
}

Aes::Aes(const std::vector<uint8_t>& key)
{
    m_handle = malloc(sizeof(AES_KEY));

    if (!m_handle)
    {
        throw std::bad_alloc();
    }
    const int result = AES_set_decrypt_key(key.data(), key.size() * 8, static_cast<AES_KEY*>(m_handle));

    if (0 != result)
    {
        throw std::runtime_error("failed to create AES key");
    }
}

Aes::~Aes()
{
    free(m_handle);
}

void Aes::Decrypt(const unsigned char* in, unsigned char* out, size_t size, uint8_t* iv, size_t)
{
    const std::lock_guard<std::mutex> guard(m_mtx);
    AES_cbc_encrypt(in, out, size, static_cast<AES_KEY*>(m_handle), iv, AES_DECRYPT);
}

#endif

} // namespace Crypto