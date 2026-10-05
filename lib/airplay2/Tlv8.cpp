#include "airplay2/Tlv8.h"

using namespace std;

namespace AirPlay2
{
    void Tlv8::Add(TlvType type, const Bytes& value)
    {
        m_items.emplace_back(static_cast<uint8_t>(type), value);
    }

    void Tlv8::Add(TlvType type, uint8_t value)
    {
        m_items.emplace_back(static_cast<uint8_t>(type), Bytes{ value });
    }

    const Bytes* Tlv8::Get(TlvType type) const noexcept
    {
        for (const auto& item : m_items)
        {
            if (item.first == static_cast<uint8_t>(type))
            {
                return &item.second;
            }
        }
        return nullptr;
    }

    bool Tlv8::GetByte(TlvType type, uint8_t& value) const noexcept
    {
        const auto* item = Get(type);

        if (item && item->size() == 1)
        {
            value = (*item)[0];
            return true;
        }
        return false;
    }

    Bytes Tlv8::Encode() const
    {
        Bytes out;

        for (const auto& item : m_items)
        {
            const auto& value = item.second;
            size_t offset = 0;

            do
            {
                const size_t chunk = min<size_t>(255, value.size() - offset);
                out.push_back(item.first);
                out.push_back(static_cast<uint8_t>(chunk));
                out.insert(out.end(), value.begin() + offset, value.begin() + offset + chunk);
                offset += chunk;
            } while (offset < value.size());
        }
        return out;
    }

    bool Tlv8::Decode(const uint8_t* data, size_t len, Tlv8& result)
    {
        result.m_items.clear();
        size_t pos = 0;
        int lastType = -1;
        size_t lastChunk = 0;

        while (pos < len)
        {
            if (pos + 2 > len)
            {
                return false;
            }
            const uint8_t type = data[pos];
            const size_t chunk = data[pos + 1];
            pos += 2;

            if (pos + chunk > len)
            {
                return false;
            }
            // a fragment continues the previous item if the type repeats after a full (255 byte) chunk
            if (lastType == type && lastChunk == 255 && !result.m_items.empty())
            {
                auto& value = result.m_items.back().second;
                value.insert(value.end(), data + pos, data + pos + chunk);
            }
            else
            {
                result.m_items.emplace_back(type, Bytes(data + pos, data + pos + chunk));
            }
            lastType = type;
            lastChunk = chunk;
            pos += chunk;
        }
        return true;
    }
}
