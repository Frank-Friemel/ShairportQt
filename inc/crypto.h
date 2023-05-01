#pragma once

#include <vector>
#include <mutex>

namespace Crypto
{
    class Rsa
    {
    public:
        Rsa();
        ~Rsa();

        std::vector<uint8_t> Sign(const std::vector<uint8_t>& input) const;
        std::vector<uint8_t> Decrypt(const std::vector<uint8_t>& input) const;

    private:
        void* m_handle;
        mutable std::mutex m_mtx;
    };

    class Aes
    {
    public:
        Aes(const std::vector<uint8_t>& key);
        ~Aes();

        void Decrypt(const unsigned char* in, unsigned char* out, size_t size, uint8_t* iv, size_t ivLen);
    
    private:
        void* m_handle;
        mutable std::mutex m_mtx;
    };
} // namespace Crypto