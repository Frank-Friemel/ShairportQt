#pragma once

#include "airplay2/Ap2Crypto.h"
#include <memory>
#include <string>

namespace AirPlay2
{
    class SrpServer;

    // Per-connection AirPlay 2 pairing state machine
    // supports "transient" pair-setup (SRP with the fixed PIN 3939) and pair-verify
    class Pairing
    {
    public:
        Pairing(const Crypto::Ed25519Key& identity, std::string deviceId);
        ~Pairing();

        // handle a POST /pair-setup body, returns the response body (TLV8)
        // the result is false if the request is malformed
        bool HandlePairSetup(const uint8_t* data, size_t len, Bytes& response);

        // handle a POST /pair-verify body, returns the response body (TLV8)
        bool HandlePairVerify(const uint8_t* data, size_t len, Bytes& response);

        // returns true (once) if the control channel encryption shall be enabled
        // after the current response has been sent; the secret is provided in that case
        bool TakeEncryptionSecret(Bytes& secret);

        // the shared secret of the session (empty if not paired)
        const Bytes& SharedSecret() const noexcept { return m_secret; }

        static constexpr const char* SetupPin = "3939";

    private:
        const Crypto::Ed25519Key    m_identity;
        const std::string           m_deviceId;
        std::unique_ptr<SrpServer>  m_srp;
        Bytes                       m_secret;
        bool                        m_enableEncryption{ false };
        bool                        m_setupDone{ false };

        // pair-verify state
        Bytes                       m_verifyPrivate;
        Bytes                       m_verifyPublic;
        Bytes                       m_verifyPeerPublic;
        Bytes                       m_verifyShared;
    };
}
