#pragma once

#include "airplay2/Ap2Crypto.h"
#include <string>

namespace AirPlay2
{
    // HAP style encrypted framing used for the RTSP control (and event) channel:
    // [uint16 LE length (<= 1024)] [ChaCha20-Poly1305 ciphertext] [16 byte tag]
    // with the length bytes as AAD and a 64-bit little-endian frame counter as nonce
    class SecureChannel
    {
    public:
        static constexpr size_t MaxFrameSize = 0x400;

        // controllerWriteInfo / controllerReadInfo: HKDF info strings from the controller's point of view
        // isServer: true if we're the accessory (we read with the controller's write key and vice versa)
        SecureChannel(const Bytes& secret, const std::string& salt, const std::string& controllerWriteInfo, const std::string& controllerReadInfo, bool isServer = true);

        static SecureChannel ForControl(const Bytes& secret, bool isServer = true)
        {
            return SecureChannel(secret, "Control-Salt", "Control-Write-Encryption-Key", "Control-Read-Encryption-Key", isServer);
        }
        static SecureChannel ForEvents(const Bytes& secret, bool isServer = true)
        {
            return SecureChannel(secret, "Events-Salt", "Events-Write-Encryption-Key", "Events-Read-Encryption-Key", isServer);
        }

        // encrypts the plain data into one or more frames
        void Encrypt(const uint8_t* data, size_t len, std::string& out);

        // feeds raw (encrypted) bytes; decrypted payload is appended to "plain"
        // returns false on an authentication failure
        bool Decrypt(const uint8_t* data, size_t len, std::string& plain);

        // true if there are incomplete frame bytes buffered
        bool HasPartialFrame() const noexcept { return !m_pending.empty(); }

    private:
        Bytes       m_readKey;
        Bytes       m_writeKey;
        uint64_t    m_readCounter{ 0 };
        uint64_t    m_writeCounter{ 0 };
        Bytes       m_pending;
    };
}
