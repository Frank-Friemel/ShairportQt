#pragma once

#include "airplay2/Ap2Crypto.h"
#include <memory>

namespace AirPlay2
{
    // SRP-6a with the RFC 5054 3072-bit group and SHA-512 (as used by HomeKit / AirPlay 2 pairing)
    class SrpServer
    {
    public:
        // privateKey: optional fixed "b" (for tests), otherwise a random value is used
        SrpServer(const std::string& username, const std::string& password, const Bytes& salt = {}, const Bytes& privateKey = {});
        ~SrpServer();

        const Bytes& Salt() const noexcept { return m_salt; }
        const Bytes& PublicKey() const noexcept { return m_B; }

        // processes the client's public key A and proof M1, on success returns the proof H(AMK)
        bool VerifyClient(const Bytes& A, const Bytes& M1, Bytes& serverProof);

        // the session key K (64 bytes), valid after a successful VerifyClient
        const Bytes& SessionKey() const noexcept { return m_K; }

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
        const std::string m_username;
        Bytes m_salt;
        Bytes m_B;
        Bytes m_K;
    };

    class SrpClient
    {
    public:
        SrpClient(const std::string& username, const std::string& password, const Bytes& privateKey = {});
        ~SrpClient();

        const Bytes& PublicKey() const noexcept { return m_A; }

        // computes the client proof M1 from the server's salt and public key
        bool ProcessChallenge(const Bytes& salt, const Bytes& B, Bytes& M1);
        bool VerifyServer(const Bytes& serverProof) const;

        const Bytes& SessionKey() const noexcept { return m_K; }

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
        const std::string m_username;
        const std::string m_password;
        Bytes m_A;
        Bytes m_M1;
        Bytes m_K;
    };
}
