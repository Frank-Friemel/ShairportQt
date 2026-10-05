#include "airplay2/SecureChannel.h"
#include <algorithm>
#include <stdexcept>

using namespace std;

namespace AirPlay2
{
    SecureChannel::SecureChannel(const Bytes& secret, const string& salt, const string& controllerWriteInfo, const string& controllerReadInfo, bool isServer)
    {
        Bytes controllerWrite = Crypto::HkdfSha512(secret, salt, controllerWriteInfo);
        Bytes controllerRead = Crypto::HkdfSha512(secret, salt, controllerReadInfo);

        if (isServer)
        {
            m_readKey = move(controllerWrite);
            m_writeKey = move(controllerRead);
        }
        else
        {
            m_readKey = move(controllerRead);
            m_writeKey = move(controllerWrite);
        }
    }

    void SecureChannel::Encrypt(const uint8_t* data, size_t len, string& out)
    {
        do
        {
            const size_t n = min(len, MaxFrameSize);
            const size_t offset = out.size();
            out.resize(offset + 2 + n + 16);

            auto* frame = reinterpret_cast<uint8_t*>(&out[offset]);
            frame[0] = static_cast<uint8_t>(n & 0xff);
            frame[1] = static_cast<uint8_t>(n >> 8);

            uint8_t nonce[12];
            Crypto::MakeCounterNonce(m_writeCounter++, nonce);

            if (!Crypto::ChaChaEncrypt(m_writeKey.data(), nonce, frame, 2, data, n, frame + 2, frame + 2 + n))
            {
                throw runtime_error("encryption failed");
            }
            data += n;
            len -= n;
        } while (len > 0);
    }

    bool SecureChannel::Decrypt(const uint8_t* data, size_t len, string& plain)
    {
        m_pending.insert(m_pending.end(), data, data + len);

        size_t pos = 0;

        while (m_pending.size() - pos >= 2)
        {
            const size_t n = m_pending[pos] | (static_cast<size_t>(m_pending[pos + 1]) << 8);

            if (n > MaxFrameSize)
            {
                return false;
            }
            if (m_pending.size() - pos < 2 + n + 16)
            {
                break;
            }
            uint8_t nonce[12];
            Crypto::MakeCounterNonce(m_readCounter++, nonce);

            const size_t offset = plain.size();
            plain.resize(offset + n);

            const uint8_t* frame = m_pending.data() + pos;

            if (!Crypto::ChaChaDecrypt(m_readKey.data(), nonce, frame, 2, frame + 2, n, frame + 2 + n, reinterpret_cast<uint8_t*>(&plain[offset])))
            {
                plain.resize(offset);
                return false;
            }
            pos += 2 + n + 16;
        }
        m_pending.erase(m_pending.begin(), m_pending.begin() + pos);
        return true;
    }
}
