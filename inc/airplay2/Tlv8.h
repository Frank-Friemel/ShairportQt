#pragma once

#include "airplay2/Ap2Crypto.h"
#include <utility>

namespace AirPlay2
{
    enum class TlvType : uint8_t
    {
        Method = 0,
        Identifier = 1,
        Salt = 2,
        PublicKey = 3,
        Proof = 4,
        EncryptedData = 5,
        State = 6,
        Error = 7,
        Signature = 10,
        Flags = 19,
    };

    enum class TlvError : uint8_t
    {
        Unknown = 1,
        Authentication = 2,
        Busy = 7,
    };

    // HomeKit style TLV8 container (values > 255 bytes are split into fragments)
    class Tlv8
    {
    public:
        void Add(TlvType type, const Bytes& value);
        void Add(TlvType type, uint8_t value);

        const Bytes* Get(TlvType type) const noexcept;
        bool GetByte(TlvType type, uint8_t& value) const noexcept;

        Bytes Encode() const;
        static bool Decode(const uint8_t* data, size_t len, Tlv8& result);

    private:
        std::vector<std::pair<uint8_t, Bytes>> m_items;
    };
}
