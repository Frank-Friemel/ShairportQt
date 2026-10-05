#include "airplay2/Plist.h"

#include <cstring>
#include <stdexcept>

using namespace std;

namespace AirPlay2
{
    namespace
    {
        const Plist& NullPlist()
        {
            static const Plist null;
            return null;
        }

        const string& EmptyString()
        {
            static const string empty;
            return empty;
        }

        const Bytes& EmptyBytes()
        {
            static const Bytes empty;
            return empty;
        }

        void AppendBE(Bytes& out, uint64_t value, size_t bytes)
        {
            for (size_t i = bytes; i > 0; --i)
            {
                out.push_back(static_cast<uint8_t>(value >> (8 * (i - 1))));
            }
        }

        uint64_t ReadBE(const uint8_t* p, size_t bytes)
        {
            uint64_t value = 0;

            for (size_t i = 0; i < bytes; ++i)
            {
                value = (value << 8) | p[i];
            }
            return value;
        }

        bool IsAscii(const string& s)
        {
            for (unsigned char c : s)
            {
                if (c >= 0x80)
                {
                    return false;
                }
            }
            return true;
        }

        u16string Utf8ToUtf16(const string& s)
        {
            u16string out;
            size_t i = 0;

            while (i < s.size())
            {
                uint32_t cp = static_cast<unsigned char>(s[i]);
                size_t extra = 0;

                if (cp >= 0xF0) { cp &= 0x07; extra = 3; }
                else if (cp >= 0xE0) { cp &= 0x0F; extra = 2; }
                else if (cp >= 0xC0) { cp &= 0x1F; extra = 1; }
                ++i;

                for (size_t k = 0; k < extra && i < s.size(); ++k, ++i)
                {
                    cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
                }
                if (cp >= 0x10000)
                {
                    cp -= 0x10000;
                    out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                    out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
                }
                else
                {
                    out.push_back(static_cast<char16_t>(cp));
                }
            }
            return out;
        }

        string Utf16ToUtf8(const u16string& s)
        {
            string out;

            for (size_t i = 0; i < s.size(); ++i)
            {
                uint32_t cp = s[i];

                if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size())
                {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (s[++i] - 0xDC00);
                }
                if (cp < 0x80)
                {
                    out.push_back(static_cast<char>(cp));
                }
                else if (cp < 0x800)
                {
                    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                else if (cp < 0x10000)
                {
                    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                else
                {
                    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
            }
            return out;
        }
    }

    Plist Plist::Bool(bool v) { Plist p; p.m_type = Type::Bool; p.m_bool = v; return p; }
    Plist Plist::Integer(int64_t v) { Plist p; p.m_type = Type::Integer; p.m_int = v; return p; }
    Plist Plist::UInteger(uint64_t v) { Plist p; p.m_type = Type::Integer; p.m_int = static_cast<int64_t>(v); p.m_unsigned = true; return p; }
    Plist Plist::Real(double v) { Plist p; p.m_type = Type::Real; p.m_real = v; return p; }
    Plist Plist::String(string v) { Plist p; p.m_type = Type::String; p.m_string = move(v); return p; }
    Plist Plist::Data(Bytes v) { Plist p; p.m_type = Type::Data; p.m_data = move(v); return p; }
    Plist Plist::Array() { Plist p; p.m_type = Type::Array; return p; }
    Plist Plist::Dict() { Plist p; p.m_type = Type::Dict; return p; }

    bool Plist::AsBool(bool def) const noexcept
    {
        if (m_type == Type::Bool) return m_bool;
        if (m_type == Type::Integer) return m_int != 0;
        return def;
    }

    int64_t Plist::AsInt(int64_t def) const noexcept
    {
        if (m_type == Type::Integer) return m_int;
        if (m_type == Type::Real) return static_cast<int64_t>(m_real);
        if (m_type == Type::Bool) return m_bool ? 1 : 0;
        return def;
    }

    uint64_t Plist::AsUInt(uint64_t def) const noexcept
    {
        if (m_type == Type::Integer) return static_cast<uint64_t>(m_int);
        if (m_type == Type::Real && m_real >= 0) return static_cast<uint64_t>(m_real);
        if (m_type == Type::Bool) return m_bool ? 1 : 0;
        return def;
    }

    double Plist::AsReal(double def) const noexcept
    {
        if (m_type == Type::Real) return m_real;
        if (m_type == Type::Integer) return m_unsigned ? static_cast<double>(static_cast<uint64_t>(m_int)) : static_cast<double>(m_int);
        return def;
    }

    const string& Plist::AsString() const noexcept
    {
        return m_type == Type::String ? m_string : EmptyString();
    }

    const Bytes& Plist::AsData() const noexcept
    {
        return m_type == Type::Data ? m_data : EmptyBytes();
    }

    size_t Plist::Size() const noexcept
    {
        return (m_type == Type::Array || m_type == Type::Dict) ? m_items.size() : 0;
    }

    const Plist& Plist::operator[](size_t i) const noexcept
    {
        return (m_type == Type::Array && i < m_items.size()) ? m_items[i] : NullPlist();
    }

    Plist& Plist::Append(Plist v)
    {
        if (m_type != Type::Array)
        {
            throw logic_error("plist is not an array");
        }
        m_items.emplace_back(move(v));
        return m_items.back();
    }

    const Plist& Plist::Get(const string& key) const noexcept
    {
        if (m_type == Type::Dict)
        {
            for (size_t i = 0; i < m_keys.size(); ++i)
            {
                if (m_keys[i] == key)
                {
                    return m_items[i];
                }
            }
        }
        return NullPlist();
    }

    bool Plist::Has(const string& key) const noexcept
    {
        if (m_type == Type::Dict)
        {
            for (const auto& k : m_keys)
            {
                if (k == key)
                {
                    return true;
                }
            }
        }
        return false;
    }

    Plist& Plist::Set(const string& key, Plist v)
    {
        if (m_type != Type::Dict)
        {
            throw logic_error("plist is not a dictionary");
        }
        for (size_t i = 0; i < m_keys.size(); ++i)
        {
            if (m_keys[i] == key)
            {
                m_items[i] = move(v);
                return m_items[i];
            }
        }
        m_keys.push_back(key);
        m_items.emplace_back(move(v));
        return m_items.back();
    }

    class PlistWriter
    {
    public:
        Bytes Write(const Plist& root)
        {
            Collect(root);

            const size_t count = m_nodes.size();
            m_refSize = count < 0x100 ? 1 : (count < 0x10000 ? 2 : 4);

            Bytes out{ 'b', 'p', 'l', 'i', 's', 't', '0', '0' };
            vector<uint64_t> offsets;
            offsets.reserve(count);

            for (const auto& node : m_nodes)
            {
                offsets.push_back(out.size());
                WriteNode(out, node);
            }
            const uint64_t offsetTable = out.size();
            const size_t offsetSize = offsetTable < 0x100 ? 1 : (offsetTable < 0x10000 ? 2 : (offsetTable < 0x100000000ULL ? 4 : 8));

            for (auto offset : offsets)
            {
                AppendBE(out, offset, offsetSize);
            }
            // trailer
            out.insert(out.end(), 6, 0);
            out.push_back(static_cast<uint8_t>(offsetSize));
            out.push_back(static_cast<uint8_t>(m_refSize));
            AppendBE(out, count, 8);
            AppendBE(out, 0, 8);
            AppendBE(out, offsetTable, 8);
            return out;
        }

    private:
        struct Node
        {
            const Plist* value{ nullptr };
            const string* key{ nullptr };
            vector<size_t> refs;
        };

        size_t Collect(const Plist& v)
        {
            const size_t index = m_nodes.size();
            m_nodes.push_back(Node{ &v, nullptr, {} });

            if (v.m_type == Plist::Type::Array)
            {
                vector<size_t> refs;

                for (const auto& item : v.m_items)
                {
                    refs.push_back(Collect(item));
                }
                m_nodes[index].refs = move(refs);
            }
            else if (v.m_type == Plist::Type::Dict)
            {
                vector<size_t> refs;

                for (const auto& key : v.m_keys)
                {
                    refs.push_back(m_nodes.size());
                    m_nodes.push_back(Node{ nullptr, &key, {} });
                }
                for (const auto& item : v.m_items)
                {
                    refs.push_back(Collect(item));
                }
                m_nodes[index].refs = move(refs);
            }
            return index;
        }

        static void WriteMarker(Bytes& out, uint8_t type, size_t count)
        {
            if (count < 15)
            {
                out.push_back(static_cast<uint8_t>(type | count));
            }
            else
            {
                out.push_back(static_cast<uint8_t>(type | 0x0F));
                WriteInt(out, count, false);
            }
        }

        static void WriteInt(Bytes& out, uint64_t value, bool isSigned)
        {
            if (isSigned && static_cast<int64_t>(value) < 0)
            {
                out.push_back(0x13);
                AppendBE(out, value, 8);
            }
            else if (value < 0x100)
            {
                out.push_back(0x10);
                AppendBE(out, value, 1);
            }
            else if (value < 0x10000)
            {
                out.push_back(0x11);
                AppendBE(out, value, 2);
            }
            else if (value < 0x100000000ULL)
            {
                out.push_back(0x12);
                AppendBE(out, value, 4);
            }
            else if (value < 0x8000000000000000ULL)
            {
                out.push_back(0x13);
                AppendBE(out, value, 8);
            }
            else
            {
                // unsigned 64 bit values are stored as 128 bit integers
                out.push_back(0x14);
                AppendBE(out, 0, 8);
                AppendBE(out, value, 8);
            }
        }

        static void WriteString(Bytes& out, const string& s)
        {
            if (IsAscii(s))
            {
                WriteMarker(out, 0x50, s.size());
                out.insert(out.end(), s.begin(), s.end());
            }
            else
            {
                const auto u16 = Utf8ToUtf16(s);
                WriteMarker(out, 0x60, u16.size());

                for (char16_t c : u16)
                {
                    AppendBE(out, c, 2);
                }
            }
        }

        void WriteNode(Bytes& out, const Node& node)
        {
            if (node.key)
            {
                WriteString(out, *node.key);
                return;
            }
            const Plist& v = *node.value;

            switch (v.m_type)
            {
            case Plist::Type::Null:
                out.push_back(0x00);
                break;
            case Plist::Type::Bool:
                out.push_back(v.m_bool ? 0x09 : 0x08);
                break;
            case Plist::Type::Integer:
                WriteInt(out, static_cast<uint64_t>(v.m_int), !v.m_unsigned);
                break;
            case Plist::Type::Real:
            {
                uint64_t bits;
                static_assert(sizeof(bits) == sizeof(v.m_real));
                memcpy(&bits, &v.m_real, sizeof(bits));
                out.push_back(0x23);
                AppendBE(out, bits, 8);
                break;
            }
            case Plist::Type::String:
                WriteString(out, v.m_string);
                break;
            case Plist::Type::Data:
                WriteMarker(out, 0x40, v.m_data.size());
                out.insert(out.end(), v.m_data.begin(), v.m_data.end());
                break;
            case Plist::Type::Array:
                WriteMarker(out, 0xA0, node.refs.size());
                for (auto ref : node.refs) AppendBE(out, ref, m_refSize);
                break;
            case Plist::Type::Dict:
                WriteMarker(out, 0xD0, node.refs.size() / 2);
                for (auto ref : node.refs) AppendBE(out, ref, m_refSize);
                break;
            }
        }

        vector<Node> m_nodes;
        size_t m_refSize{ 1 };
    };

    class PlistReader
    {
    public:
        PlistReader(const uint8_t* data, size_t len)
            : m_data{ data }
            , m_len{ len }
            , m_nodeBudget{ 4 * static_cast<uint64_t>(len) + 64 }
            , m_byteBudget{ 4 * static_cast<uint64_t>(len) + 64 * 1024 }
        {
        }

        bool Read(Plist& result)
        {
            if (m_len < 8 + 32 || memcmp(m_data, "bplist00", 8) != 0)
            {
                return false;
            }
            const uint8_t* trailer = m_data + m_len - 32;
            m_offsetSize = trailer[6];
            m_refSize = trailer[7];
            m_numObjects = ReadBE(trailer + 8, 8);
            const uint64_t top = ReadBE(trailer + 16, 8);
            m_offsetTable = ReadBE(trailer + 24, 8);

            if (m_offsetSize < 1 || m_offsetSize > 8 || m_refSize < 1 || m_refSize > 8 ||
                top >= m_numObjects || m_numObjects > m_len ||
                m_offsetTable >= m_len - 32 ||
                m_numObjects * m_offsetSize > m_len - 32 - m_offsetTable)
            {
                return false;
            }
            return ReadObject(top, result, 0);
        }

    private:
        bool ObjectOffset(uint64_t index, uint64_t& offset) const
        {
            if (index >= m_numObjects)
            {
                return false;
            }
            offset = ReadBE(m_data + m_offsetTable + index * m_offsetSize, m_offsetSize);
            return offset >= 8 && offset < m_offsetTable;
        }

        bool Available(uint64_t pos, uint64_t n) const
        {
            return pos <= m_offsetTable && n <= m_offsetTable - pos;
        }

        bool ReadCount(uint64_t& pos, uint8_t marker, uint64_t& count) const
        {
            count = marker & 0x0F;

            if (count == 0x0F)
            {
                if (!Available(pos, 1))
                {
                    return false;
                }
                const uint8_t intMarker = m_data[pos++];

                if ((intMarker & 0xF0) != 0x10)
                {
                    return false;
                }
                const size_t bytes = size_t(1) << (intMarker & 0x0F);

                if (bytes > 8 || !Available(pos, bytes))
                {
                    return false;
                }
                count = ReadBE(m_data + pos, bytes);
                pos += bytes;
            }
            return true;
        }

        bool ReadObject(uint64_t index, Plist& result, int depth)
        {
            uint64_t pos = 0;

            // objects may be referenced multiple times: limit the total work
            // so that a small malicious plist can't expand exponentially
            if (depth > 32 || m_nodeBudget == 0 || !ObjectOffset(index, pos))
            {
                return false;
            }
            --m_nodeBudget;

            const uint8_t marker = m_data[pos++];
            const uint8_t type = marker & 0xF0;

            switch (type)
            {
            case 0x00:
                if (marker == 0x08 || marker == 0x09)
                {
                    result = Plist::Bool(marker == 0x09);
                }
                else
                {
                    result = Plist();
                }
                return true;
            case 0x10:
            {
                const size_t bytes = size_t(1) << (marker & 0x0F);

                if (bytes > 16 || !Available(pos, bytes))
                {
                    return false;
                }
                if (bytes == 16)
                {
                    result = Plist::UInteger(ReadBE(m_data + pos + 8, 8));
                }
                else if (bytes == 8)
                {
                    result = Plist::Integer(static_cast<int64_t>(ReadBE(m_data + pos, 8)));
                }
                else
                {
                    result = Plist::Integer(static_cast<int64_t>(ReadBE(m_data + pos, bytes)));
                }
                return true;
            }
            case 0x20:
            case 0x30: // date (seconds since 2001) is treated as a real
            {
                const size_t bytes = size_t(1) << (marker & 0x0F);

                if (!Available(pos, bytes))
                {
                    return false;
                }
                if (bytes == 4)
                {
                    const uint32_t bits = static_cast<uint32_t>(ReadBE(m_data + pos, 4));
                    float f;
                    memcpy(&f, &bits, 4);
                    result = Plist::Real(f);
                }
                else if (bytes == 8)
                {
                    const uint64_t bits = ReadBE(m_data + pos, 8);
                    double d;
                    memcpy(&d, &bits, 8);
                    result = Plist::Real(d);
                }
                else
                {
                    return false;
                }
                return true;
            }
            case 0x40:
            case 0x50:
            {
                uint64_t count = 0;

                if (!ReadCount(pos, marker, count) || !Available(pos, count) || count > m_byteBudget)
                {
                    return false;
                }
                m_byteBudget -= count;

                if (type == 0x40)
                {
                    result = Plist::Data(Bytes(m_data + pos, m_data + pos + count));
                }
                else
                {
                    result = Plist::String(string(reinterpret_cast<const char*>(m_data + pos), static_cast<size_t>(count)));
                }
                return true;
            }
            case 0x60:
            {
                uint64_t count = 0;

                if (!ReadCount(pos, marker, count) || count > m_len || !Available(pos, count * 2) || count * 3 > m_byteBudget)
                {
                    return false;
                }
                m_byteBudget -= count * 3;

                u16string s;
                s.reserve(static_cast<size_t>(count));

                for (uint64_t i = 0; i < count; ++i)
                {
                    s.push_back(static_cast<char16_t>(ReadBE(m_data + pos + i * 2, 2)));
                }
                result = Plist::String(Utf16ToUtf8(s));
                return true;
            }
            case 0x80:
            {
                const size_t bytes = (marker & 0x0F) + 1;

                if (bytes > 8 || !Available(pos, bytes))
                {
                    return false;
                }
                result = Plist::UInteger(ReadBE(m_data + pos, bytes));
                return true;
            }
            case 0xA0:
            case 0xC0:
            {
                uint64_t count = 0;

                if (!ReadCount(pos, marker, count) || count > m_numObjects || !Available(pos, count * m_refSize))
                {
                    return false;
                }
                result = Plist::Array();

                for (uint64_t i = 0; i < count; ++i)
                {
                    Plist item;

                    if (!ReadObject(ReadBE(m_data + pos + i * m_refSize, m_refSize), item, depth + 1))
                    {
                        return false;
                    }
                    result.Append(move(item));
                }
                return true;
            }
            case 0xD0:
            {
                uint64_t count = 0;

                if (!ReadCount(pos, marker, count) || count > m_numObjects || !Available(pos, count * 2 * m_refSize))
                {
                    return false;
                }
                result = Plist::Dict();

                for (uint64_t i = 0; i < count; ++i)
                {
                    Plist key;
                    Plist value;

                    if (!ReadObject(ReadBE(m_data + pos + i * m_refSize, m_refSize), key, depth + 1) ||
                        key.GetType() != Plist::Type::String ||
                        !ReadObject(ReadBE(m_data + pos + (count + i) * m_refSize, m_refSize), value, depth + 1))
                    {
                        return false;
                    }
                    result.Set(key.AsString(), move(value));
                }
                return true;
            }
            default:
                return false;
            }
        }

        const uint8_t* const m_data;
        const size_t m_len;
        size_t m_offsetSize{ 0 };
        size_t m_refSize{ 0 };
        uint64_t m_numObjects{ 0 };
        uint64_t m_offsetTable{ 0 };
        uint64_t m_nodeBudget{ 0 };
        uint64_t m_byteBudget{ 0 };
    };

    Bytes Plist::ToBinary() const
    {
        return PlistWriter().Write(*this);
    }

    bool Plist::FromBinary(const uint8_t* data, size_t len, Plist& result)
    {
        try
        {
            return data && PlistReader(data, len).Read(result);
        }
        catch (...)
        {
            return false;
        }
    }
}
