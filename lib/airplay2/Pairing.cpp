#include "airplay2/Pairing.h"
#include "airplay2/Srp.h"
#include "airplay2/Tlv8.h"
#include <spdlog/spdlog.h>

using namespace std;

namespace AirPlay2
{
    namespace
    {
        constexpr uint8_t FlagTransient = 0x10;

        Bytes ErrorResponse(uint8_t state, TlvError error)
        {
            Tlv8 tlv;
            tlv.Add(TlvType::State, state);
            tlv.Add(TlvType::Error, static_cast<uint8_t>(error));
            return tlv.Encode();
        }

        Bytes Concat(initializer_list<const Bytes*> parts)
        {
            Bytes r;
            for (const auto* p : parts)
            {
                r.insert(r.end(), p->begin(), p->end());
            }
            return r;
        }
    }

    Pairing::Pairing(const Crypto::Ed25519Key& identity, string deviceId)
        : m_identity(identity)
        , m_deviceId(move(deviceId))
    {
    }

    Pairing::~Pairing() = default;

    bool Pairing::TakeEncryptionSecret(Bytes& secret)
    {
        if (!m_enableEncryption)
        {
            return false;
        }
        m_enableEncryption = false;
        secret = m_secret;
        return true;
    }

    bool Pairing::HandlePairSetup(const uint8_t* data, size_t len, Bytes& response)
    {
        Tlv8 request;

        if (!Tlv8::Decode(data, len, request))
        {
            return false;
        }
        uint8_t state = 0;

        if (!request.GetByte(TlvType::State, state))
        {
            return false;
        }

        if (state == 1)
        {
            uint8_t flags = 0;
            request.GetByte(TlvType::Flags, flags);

            m_transient = (flags & FlagTransient) != 0;
            m_setupKey.clear();

            if (!m_transient)
            {
                spdlog::info("AirPlay2: full pair-setup requested (flags {:#x})", flags);
            }
            m_srp = make_unique<SrpServer>("Pair-Setup", SetupPin);

            Tlv8 tlv;
            tlv.Add(TlvType::State, 2);
            tlv.Add(TlvType::PublicKey, m_srp->PublicKey());
            tlv.Add(TlvType::Salt, m_srp->Salt());
            response = tlv.Encode();
            return true;
        }

        if (state == 3)
        {
            const Bytes* A = request.Get(TlvType::PublicKey);
            const Bytes* proof = request.Get(TlvType::Proof);

            if (!m_srp || !A || !proof)
            {
                response = ErrorResponse(4, TlvError::Authentication);
                return true;
            }
            Bytes serverProof;

            if (!m_srp->VerifyClient(*A, *proof, serverProof))
            {
                spdlog::warn("AirPlay2: pair-setup SRP verification failed");
                m_srp.reset();
                response = ErrorResponse(4, TlvError::Authentication);
                return true;
            }
            const Bytes sessionKey = m_srp->SessionKey();
            m_srp.reset();

            Tlv8 tlv;
            tlv.Add(TlvType::State, 4);
            tlv.Add(TlvType::Proof, serverProof);
            response = tlv.Encode();

            if (m_transient)
            {
                // the response is still sent unencrypted, everything afterwards is encrypted
                m_secret = sessionKey;
                m_enableEncryption = true;
                spdlog::info("AirPlay2: transient pair-setup completed");
            }
            else
            {
                // the full pair-setup continues with M5/M6 on the plain connection
                m_setupKey = sessionKey;
            }
            return true;
        }

        if (state == 5)
        {
            response = HandlePairSetupM5(request);
            return true;
        }
        return false;
    }

    bool Pairing::HandlePairVerify(const uint8_t* data, size_t len, Bytes& response)
    {
        Tlv8 request;

        if (!Tlv8::Decode(data, len, request))
        {
            return false;
        }
        uint8_t state = 0;

        if (!request.GetByte(TlvType::State, state))
        {
            return false;
        }

        if (state == 1)
        {
            const Bytes* peerPublic = request.Get(TlvType::PublicKey);

            if (!peerPublic || peerPublic->size() != 32)
            {
                response = ErrorResponse(2, TlvError::Authentication);
                return true;
            }
            m_verifyPeerPublic = *peerPublic;
            Crypto::X25519Generate(m_verifyPrivate, m_verifyPublic);
            m_verifyShared = Crypto::X25519Shared(m_verifyPrivate, m_verifyPeerPublic);

            const Bytes id(m_deviceId.begin(), m_deviceId.end());
            const Bytes info = Concat({ &m_verifyPublic, &id, &m_verifyPeerPublic });

            Tlv8 sub;
            sub.Add(TlvType::Identifier, id);
            sub.Add(TlvType::Signature, m_identity.Sign(info));
            const Bytes plain = sub.Encode();

            const Bytes key = Crypto::HkdfSha512(m_verifyShared, "Pair-Verify-Encrypt-Salt", "Pair-Verify-Encrypt-Info");
            uint8_t nonce[12];
            Crypto::MakeLabelNonce("PV-Msg02", nonce);

            Bytes encrypted(plain.size() + 16);

            if (!Crypto::ChaChaEncrypt(key.data(), nonce, nullptr, 0, plain.data(), plain.size(), encrypted.data(), encrypted.data() + plain.size()))
            {
                return false;
            }
            Tlv8 tlv;
            tlv.Add(TlvType::State, 2);
            tlv.Add(TlvType::PublicKey, m_verifyPublic);
            tlv.Add(TlvType::EncryptedData, encrypted);
            response = tlv.Encode();
            return true;
        }

        if (state == 3)
        {
            const Bytes* encrypted = request.Get(TlvType::EncryptedData);

            if (m_verifyShared.empty() || !encrypted || encrypted->size() < 16)
            {
                response = ErrorResponse(4, TlvError::Authentication);
                return true;
            }
            const Bytes key = Crypto::HkdfSha512(m_verifyShared, "Pair-Verify-Encrypt-Salt", "Pair-Verify-Encrypt-Info");
            uint8_t nonce[12];
            Crypto::MakeLabelNonce("PV-Msg03", nonce);

            const size_t plainLen = encrypted->size() - 16;
            Bytes plain(plainLen);

            if (!Crypto::ChaChaDecrypt(key.data(), nonce, nullptr, 0, encrypted->data(), plainLen, encrypted->data() + plainLen, plain.data()))
            {
                spdlog::warn("AirPlay2: pair-verify M3 decryption failed");
                response = ErrorResponse(4, TlvError::Authentication);
                return true;
            }
            // we don't keep a list of paired controllers, therefore the controller's signature
            // can't be checked against a long-term public key (see doc/AirPlay2-OptionalFeatures.md)
            // the connection stays unencrypted, the transient pair-setup follows (like shairport-sync)
            Tlv8 tlv;
            tlv.Add(TlvType::State, 4);
            response = tlv.Encode();

            spdlog::info("AirPlay2: pair-verify completed");
            return true;
        }
        return false;
    }

    Bytes Pairing::HandlePairSetupM5(const Tlv8& request)
    {
        const Bytes* encrypted = request.Get(TlvType::EncryptedData);

        if (m_setupKey.empty() || !encrypted || encrypted->size() < 16)
        {
            spdlog::warn("AirPlay2: unexpected pair-setup M5");
            return ErrorResponse(6, TlvError::Authentication);
        }
        const Bytes setupKey = move(m_setupKey);
        m_setupKey.clear();

        const Bytes key = Crypto::HkdfSha512(setupKey, "Pair-Setup-Encrypt-Salt", "Pair-Setup-Encrypt-Info");
        uint8_t nonce[12];
        Crypto::MakeLabelNonce("PS-Msg05", nonce);

        const size_t plainLen = encrypted->size() - 16;
        Bytes plain(plainLen);

        if (!Crypto::ChaChaDecrypt(key.data(), nonce, nullptr, 0, encrypted->data(), plainLen, encrypted->data() + plainLen, plain.data()))
        {
            spdlog::warn("AirPlay2: pair-setup M5 decryption failed");
            return ErrorResponse(6, TlvError::Authentication);
        }
        Tlv8 sub;

        if (!Tlv8::Decode(plain.data(), plain.size(), sub))
        {
            return ErrorResponse(6, TlvError::Authentication);
        }
        const Bytes* controllerId = sub.Get(TlvType::Identifier);
        const Bytes* controllerKey = sub.Get(TlvType::PublicKey);
        const Bytes* controllerSignature = sub.Get(TlvType::Signature);

        if (!controllerId || !controllerKey || controllerKey->size() != 32 || !controllerSignature || controllerSignature->size() != 64)
        {
            spdlog::warn("AirPlay2: pair-setup M5 without controller identity");
            return ErrorResponse(6, TlvError::Authentication);
        }
        const Bytes controllerX = Crypto::HkdfSha512(setupKey, "Pair-Setup-Controller-Sign-Salt", "Pair-Setup-Controller-Sign-Info");

        if (!Crypto::Ed25519Key::Verify(*controllerKey, Concat({ &controllerX, controllerId, controllerKey }), *controllerSignature))
        {
            spdlog::warn("AirPlay2: pair-setup M5 signature verification failed");
            return ErrorResponse(6, TlvError::Authentication);
        }

        // M6: our long-term identity, signed and encrypted
        const Bytes id(m_deviceId.begin(), m_deviceId.end());
        const Bytes accessoryX = Crypto::HkdfSha512(setupKey, "Pair-Setup-Accessory-Sign-Salt", "Pair-Setup-Accessory-Sign-Info");

        Tlv8 accessory;
        accessory.Add(TlvType::Identifier, id);
        accessory.Add(TlvType::PublicKey, m_identity.PublicKey());
        accessory.Add(TlvType::Signature, m_identity.Sign(Concat({ &accessoryX, &id, &m_identity.PublicKey() })));
        const Bytes accessoryPlain = accessory.Encode();

        Crypto::MakeLabelNonce("PS-Msg06", nonce);
        Bytes accessoryEncrypted(accessoryPlain.size() + 16);

        if (!Crypto::ChaChaEncrypt(key.data(), nonce, nullptr, 0, accessoryPlain.data(), accessoryPlain.size(),
            accessoryEncrypted.data(), accessoryEncrypted.data() + accessoryPlain.size()))
        {
            return ErrorResponse(6, TlvError::Unknown);
        }
        Tlv8 tlv;
        tlv.Add(TlvType::State, 6);
        tlv.Add(TlvType::EncryptedData, accessoryEncrypted);

        // the controller isn't persisted (see doc/AirPlay2-OptionalFeatures.md)
        spdlog::info("AirPlay2: full pair-setup completed with controller {}", string(controllerId->begin(), controllerId->end()));
        return tlv.Encode();
    }
}
