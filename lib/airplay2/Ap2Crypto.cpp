#include "airplay2/Ap2Crypto.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <cstring>
#include <memory>
#include <stdexcept>

using namespace std;

namespace AirPlay2::Crypto
{
    namespace
    {
        struct CipherCtxDeleter { void operator()(EVP_CIPHER_CTX* p) const { EVP_CIPHER_CTX_free(p); } };
        struct PKeyDeleter { void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); } };
        struct PKeyCtxDeleter { void operator()(EVP_PKEY_CTX* p) const { EVP_PKEY_CTX_free(p); } };
        struct MdCtxDeleter { void operator()(EVP_MD_CTX* p) const { EVP_MD_CTX_free(p); } };

        using CipherCtxPtr = unique_ptr<EVP_CIPHER_CTX, CipherCtxDeleter>;
        using PKeyPtr = unique_ptr<EVP_PKEY, PKeyDeleter>;
        using PKeyCtxPtr = unique_ptr<EVP_PKEY_CTX, PKeyCtxDeleter>;
        using MdCtxPtr = unique_ptr<EVP_MD_CTX, MdCtxDeleter>;

        Bytes RawPublicKey(EVP_PKEY* key)
        {
            size_t len = 32;
            Bytes pub(len);

            if (EVP_PKEY_get_raw_public_key(key, pub.data(), &len) != 1 || len != 32)
            {
                throw runtime_error("failed to get raw public key");
            }
            return pub;
        }

        Bytes RawPrivateKey(EVP_PKEY* key)
        {
            size_t len = 32;
            Bytes priv(len);

            if (EVP_PKEY_get_raw_private_key(key, priv.data(), &len) != 1 || len != 32)
            {
                throw runtime_error("failed to get raw private key");
            }
            return priv;
        }

        PKeyPtr GenerateKey(int type)
        {
            PKeyCtxPtr ctx{ EVP_PKEY_CTX_new_id(type, nullptr) };
            EVP_PKEY* key = nullptr;

            if (!ctx || EVP_PKEY_keygen_init(ctx.get()) != 1 || EVP_PKEY_keygen(ctx.get(), &key) != 1)
            {
                throw runtime_error("key generation failed");
            }
            return PKeyPtr{ key };
        }
    }

    Bytes RandomBytes(size_t n)
    {
        Bytes result(n);

        if (n && RAND_bytes(result.data(), static_cast<int>(n)) != 1)
        {
            throw runtime_error("RAND_bytes failed");
        }
        return result;
    }

    Bytes Sha512(const void* data, size_t len)
    {
        Bytes digest(SHA512_DIGEST_LENGTH);
        SHA512(static_cast<const unsigned char*>(data), len, digest.data());
        return digest;
    }

    Bytes Sha512(initializer_list<const Bytes*> parts)
    {
        MdCtxPtr ctx{ EVP_MD_CTX_new() };

        if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha512(), nullptr) != 1)
        {
            throw runtime_error("SHA-512 init failed");
        }
        for (const auto* part : parts)
        {
            if (part && !part->empty())
            {
                EVP_DigestUpdate(ctx.get(), part->data(), part->size());
            }
        }
        Bytes digest(SHA512_DIGEST_LENGTH);
        unsigned int len = 0;
        EVP_DigestFinal_ex(ctx.get(), digest.data(), &len);
        return digest;
    }

    Bytes HkdfSha512(const Bytes& ikm, const string& salt, const string& info, size_t outLen)
    {
        PKeyCtxPtr ctx{ EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr) };
        Bytes out(outLen);

        if (!ctx ||
            EVP_PKEY_derive_init(ctx.get()) != 1 ||
            EVP_PKEY_CTX_set_hkdf_md(ctx.get(), EVP_sha512()) != 1 ||
            EVP_PKEY_CTX_set1_hkdf_salt(ctx.get(), reinterpret_cast<const unsigned char*>(salt.data()), static_cast<int>(salt.size())) != 1 ||
            EVP_PKEY_CTX_set1_hkdf_key(ctx.get(), ikm.data(), static_cast<int>(ikm.size())) != 1 ||
            EVP_PKEY_CTX_add1_hkdf_info(ctx.get(), reinterpret_cast<const unsigned char*>(info.data()), static_cast<int>(info.size())) != 1 ||
            EVP_PKEY_derive(ctx.get(), out.data(), &outLen) != 1)
        {
            throw runtime_error("HKDF failed");
        }
        out.resize(outLen);
        return out;
    }

    bool ChaChaEncrypt(const uint8_t* key, const uint8_t* nonce12, const uint8_t* aad, size_t aadLen,
        const uint8_t* plain, size_t len, uint8_t* out, uint8_t* tag16)
    {
        CipherCtxPtr ctx{ EVP_CIPHER_CTX_new() };
        int outLen = 0;

        if (!ctx ||
            EVP_EncryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1 ||
            EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr) != 1 ||
            EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key, nonce12) != 1)
        {
            return false;
        }
        if (aad && aadLen && EVP_EncryptUpdate(ctx.get(), nullptr, &outLen, aad, static_cast<int>(aadLen)) != 1)
        {
            return false;
        }
        if (len && EVP_EncryptUpdate(ctx.get(), out, &outLen, plain, static_cast<int>(len)) != 1)
        {
            return false;
        }
        if (EVP_EncryptFinal_ex(ctx.get(), out + (len ? outLen : 0), &outLen) != 1)
        {
            return false;
        }
        return EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_GET_TAG, 16, tag16) == 1;
    }

    bool ChaChaDecrypt(const uint8_t* key, const uint8_t* nonce12, const uint8_t* aad, size_t aadLen,
        const uint8_t* cipher, size_t len, const uint8_t* tag16, uint8_t* out)
    {
        CipherCtxPtr ctx{ EVP_CIPHER_CTX_new() };
        int outLen = 0;

        if (!ctx ||
            EVP_DecryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1 ||
            EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr) != 1 ||
            EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key, nonce12) != 1)
        {
            return false;
        }
        if (aad && aadLen && EVP_DecryptUpdate(ctx.get(), nullptr, &outLen, aad, static_cast<int>(aadLen)) != 1)
        {
            return false;
        }
        if (len && EVP_DecryptUpdate(ctx.get(), out, &outLen, cipher, static_cast<int>(len)) != 1)
        {
            return false;
        }
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_TAG, 16, const_cast<uint8_t*>(tag16)) != 1)
        {
            return false;
        }
        uint8_t finalBuf[16];
        return EVP_DecryptFinal_ex(ctx.get(), len ? out + outLen : finalBuf, &outLen) == 1;
    }

    void MakeCounterNonce(uint64_t counter, uint8_t* nonce12)
    {
        memset(nonce12, 0, 4);

        for (int i = 0; i < 8; ++i)
        {
            nonce12[4 + i] = static_cast<uint8_t>(counter >> (8 * i));
        }
    }

    void MakeLabelNonce(const char* label8, uint8_t* nonce12)
    {
        memset(nonce12, 0, 4);
        memcpy(nonce12 + 4, label8, 8);
    }

    Ed25519Key Ed25519Key::Generate()
    {
        auto key = GenerateKey(EVP_PKEY_ED25519);
        Ed25519Key result;
        result.m_private = RawPrivateKey(key.get());
        result.m_public = RawPublicKey(key.get());
        return result;
    }

    Ed25519Key Ed25519Key::FromPrivate(const Bytes& priv32)
    {
        if (priv32.size() != 32)
        {
            throw invalid_argument("Ed25519 private key must be 32 bytes");
        }
        PKeyPtr key{ EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, priv32.data(), priv32.size()) };

        if (!key)
        {
            throw runtime_error("invalid Ed25519 private key");
        }
        Ed25519Key result;
        result.m_private = priv32;
        result.m_public = RawPublicKey(key.get());
        return result;
    }

    Bytes Ed25519Key::Sign(const Bytes& message) const
    {
        PKeyPtr key{ EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, m_private.data(), m_private.size()) };
        MdCtxPtr ctx{ EVP_MD_CTX_new() };

        if (!key || !ctx || EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) != 1)
        {
            throw runtime_error("Ed25519 sign init failed");
        }
        size_t sigLen = 64;
        Bytes sig(sigLen);

        if (EVP_DigestSign(ctx.get(), sig.data(), &sigLen, message.data(), message.size()) != 1)
        {
            throw runtime_error("Ed25519 sign failed");
        }
        sig.resize(sigLen);
        return sig;
    }

    bool Ed25519Key::Verify(const Bytes& publicKey, const Bytes& message, const Bytes& signature)
    {
        if (publicKey.size() != 32 || signature.size() != 64)
        {
            return false;
        }
        PKeyPtr key{ EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, publicKey.data(), publicKey.size()) };
        MdCtxPtr ctx{ EVP_MD_CTX_new() };

        if (!key || !ctx || EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) != 1)
        {
            return false;
        }
        return EVP_DigestVerify(ctx.get(), signature.data(), signature.size(), message.data(), message.size()) == 1;
    }

    void X25519Generate(Bytes& priv32, Bytes& pub32)
    {
        auto key = GenerateKey(EVP_PKEY_X25519);
        priv32 = RawPrivateKey(key.get());
        pub32 = RawPublicKey(key.get());
    }

    Bytes X25519Shared(const Bytes& priv32, const Bytes& peerPub32)
    {
        if (priv32.size() != 32 || peerPub32.size() != 32)
        {
            throw invalid_argument("X25519 keys must be 32 bytes");
        }
        PKeyPtr priv{ EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, priv32.data(), priv32.size()) };
        PKeyPtr peer{ EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peerPub32.data(), peerPub32.size()) };

        if (!priv || !peer)
        {
            throw runtime_error("invalid X25519 key");
        }
        PKeyCtxPtr ctx{ EVP_PKEY_CTX_new(priv.get(), nullptr) };
        size_t len = 32;
        Bytes shared(len);

        if (!ctx ||
            EVP_PKEY_derive_init(ctx.get()) != 1 ||
            EVP_PKEY_derive_set_peer(ctx.get(), peer.get()) != 1 ||
            EVP_PKEY_derive(ctx.get(), shared.data(), &len) != 1)
        {
            throw runtime_error("X25519 derive failed");
        }
        shared.resize(len);
        return shared;
    }
}
