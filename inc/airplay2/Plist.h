#pragma once

#include "airplay2/Ap2Crypto.h"
#include <string>
#include <vector>

namespace AirPlay2
{
    // Minimal Apple binary property list ("bplist00") value with encoder and decoder
    class Plist
    {
    public:
        enum class Type { Null, Bool, Integer, Real, String, Data, Array, Dict };

        Plist() = default;
        static Plist Bool(bool v);
        static Plist Integer(int64_t v);
        static Plist UInteger(uint64_t v);
        static Plist Real(double v);
        static Plist String(std::string v);
        static Plist Data(Bytes v);
        static Plist Array();
        static Plist Dict();

        Type GetType() const noexcept { return m_type; }
        bool IsNull() const noexcept { return m_type == Type::Null; }
        bool IsDict() const noexcept { return m_type == Type::Dict; }
        bool IsArray() const noexcept { return m_type == Type::Array; }

        bool AsBool(bool def = false) const noexcept;
        int64_t AsInt(int64_t def = 0) const noexcept;
        uint64_t AsUInt(uint64_t def = 0) const noexcept;
        double AsReal(double def = 0.) const noexcept;
        const std::string& AsString() const noexcept;
        const Bytes& AsData() const noexcept;

        // array access
        size_t Size() const noexcept;
        const Plist& operator[](size_t i) const noexcept;
        Plist& Append(Plist v);

        // dict access (returns a null value if the key doesn't exist)
        const Plist& Get(const std::string& key) const noexcept;
        bool Has(const std::string& key) const noexcept;
        Plist& Set(const std::string& key, Plist v);
        const std::vector<std::string>& Keys() const noexcept { return m_keys; }

        Bytes ToBinary() const;
        static bool FromBinary(const uint8_t* data, size_t len, Plist& result);
        static bool FromBinary(const std::string& data, Plist& result)
        {
            return FromBinary(reinterpret_cast<const uint8_t*>(data.data()), data.size(), result);
        }

    private:
        Type                    m_type{ Type::Null };
        bool                    m_bool{ false };
        int64_t                 m_int{ 0 };
        bool                    m_unsigned{ false };
        double                  m_real{ 0. };
        std::string             m_string;
        Bytes                   m_data;
        std::vector<Plist>      m_items;  // array items or dict values
        std::vector<std::string> m_keys;  // dict keys

        friend class PlistWriter;
        friend class PlistReader;
    };
}
