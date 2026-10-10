#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <initializer_list>

namespace AirPlay2
{
    using Bytes = std::vector<uint8_t>;

    namespace Crypto
    {
        Bytes RandomBytes(size_t n);

        Bytes Sha512(const void* data, size_t len);
        Bytes Sha512(std::initializer_list<const Bytes*> parts);

        // HKDF with SHA-512
        Bytes HkdfSha512(const Bytes& ikm, const std::string& salt, const std::string& info, size_t outLen = 32);

        // ChaCha20-Poly1305 (IETF, 12-byte nonce, 16-byte tag)
        bool ChaChaEncrypt(const uint8_t* key, const uint8_t* nonce12,
            const uint8_t* aad, size_t aadLen,
            const uint8_t* plain, size_t len,
            uint8_t* out, uint8_t* tag16);

        bool ChaChaDecrypt(const uint8_t* key, const uint8_t* nonce12,
            const uint8_t* aad, size_t aadLen,
            const uint8_t* cipher, size_t len,
            const uint8_t* tag16, uint8_t* out);

        // 12-byte nonce: 4 zero bytes followed by a little-endian 64-bit counter
        void MakeCounterNonce(uint64_t counter, uint8_t* nonce12);

        // 12-byte nonce: 4 zero bytes followed by an 8-character label (e.g. "PV-Msg02")
        void MakeLabelNonce(const char* label8, uint8_t* nonce12);

        class Ed25519Key
        {
        public:
            static Ed25519Key Generate();
            static Ed25519Key FromPrivate(const Bytes& priv32);

            Bytes Sign(const Bytes& message) const;
            static bool Verify(const Bytes& publicKey, const Bytes& message, const Bytes& signature);

            const Bytes& PrivateKey() const noexcept { return m_private; }
            const Bytes& PublicKey() const noexcept { return m_public; }
            bool IsValid() const noexcept { return m_private.size() == 32 && m_public.size() == 32; }

        private:
            Bytes m_private;
            Bytes m_public;
        };

        // X25519 Diffie-Hellman
        void X25519Generate(Bytes& priv32, Bytes& pub32);
        Bytes X25519Shared(const Bytes& priv32, const Bytes& peerPub32);
    }
}
