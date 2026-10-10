#pragma once

#include "LayerCake.h"
#include "airplay2/Ap2Crypto.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "sockpp/tcp_acceptor.h"

namespace AirPlay2
{
    class Ap2AudioSession;
    class Pairing;
    class PtpClock;
    class Plist;

    // transport independent view of an RTSP request
    struct Request
    {
        std::string                         method;
        std::string                         path;
        std::map<std::string, std::string>  headers;    // keys in lowercase
        std::string                         body;
        std::string                         remoteAddr;
        std::string                         localAddr;

        std::string Header(const std::string& lowerCaseKey) const;
    };

    struct Response
    {
        int                                 status{ 200 };
        std::vector<std::pair<std::string, std::string>> headers;
        std::string                         body;
        std::string                         contentType;
    };

    // callbacks into the hosting RTSP server
    class IHost
    {
    public:
        virtual ~IHost() = default;

        // a new audio session has been set up (replaces any active session)
        virtual void OnSessionStarted(const std::shared_ptr<Ap2AudioSession>& session) = 0;
        // the audio session has been torn down
        virtual void OnSessionEnded(const std::shared_ptr<Ap2AudioSession>& session) = 0;
        // DACP remote control information
        virtual void OnDacp(const std::string& dacpID, const std::string& activeRemote, const std::string& remoteAddr) = 0;
    };

    using TxtRecords = std::vector<std::pair<std::string, std::string>>;

    // per RTSP connection AirPlay 2 state
    class Connection
    {
    public:
        Connection(const Crypto::Ed25519Key& identity, const std::string& deviceId);
        ~Connection();

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;

        // true once the client used an AirPlay 2 specific request on this connection
        bool IsAirPlay2() const noexcept { return m_isAirPlay2; }

        // returns true (once) if the control channel shall be encrypted after the current response
        bool TakeEncryptionSecret(Bytes& secret);

        const std::shared_ptr<Ap2AudioSession>& Session() const noexcept { return m_session; }

    private:
        friend class Service;

        void StopEventChannel() noexcept;

        std::unique_ptr<Pairing>            m_pairing;
        bool                                m_isAirPlay2{ false };
        std::shared_ptr<Ap2AudioSession>    m_session;

        std::unique_ptr<sockpp::tcp_acceptor> m_eventAcceptor;
        std::thread                         m_eventThread;
        std::atomic_bool                    m_eventStop{ false };
    };

    // AirPlay 2 receiver service (one per RTSP server)
    class Service
    {
    public:
        Service(SharedPtr<IValueCollection> config, IHost* host);
        ~Service();

        Service(const Service&) = delete;
        Service& operator=(const Service&) = delete;

        // true if AirPlay 2 is enabled in the config and supported by this build
        static bool IsSupported() noexcept;
        bool IsEnabled() const;

        // 64-bit AirPlay feature flags
        uint64_t Features() const;

        // TXT records of the "_airplay._tcp" service
        TxtRecords AirPlayTxtRecords() const;
        // additional TXT records of the "_raop._tcp" service in AirPlay 2 mode
        TxtRecords RaopTxtRecords() const;

        std::shared_ptr<Connection> CreateConnection() const;

        // true if the request is AirPlay 2 specific (or the connection is in AirPlay 2 mode)
        bool IsAirPlay2Request(const Connection& connection, const Request& request) const;

        // handles the request, returns false if the request should be handled by the AirPlay 1 code
        bool Handle(Connection& connection, const Request& request, Response& response);

        // called when the RTSP connection has been closed
        void OnConnectionClosed(Connection& connection);

        const std::string& DeviceId() const noexcept { return m_deviceId; }

        static constexpr const char* SourceVersion = "366.0";

    private:
        void HandleInfo(const Request& request, Response& response) const;
        void HandleSetup(Connection& connection, const Request& request, Response& response);
        void HandleSetupStream(Connection& connection, const Request& request, const Plist& stream, Response& response);
        void HandleTeardown(Connection& connection, const Request& request, Response& response);
        void HandleSetRateAnchorTime(Connection& connection, const Request& request, Response& response);
        void HandleFlushBuffered(Connection& connection, const Request& request, Response& response);
        void StartEventChannel(Connection& connection);
        std::shared_ptr<PtpClock> GetClock();
        void EndSession(Connection& connection);

        std::string PublicKeyHex() const;

    private:
        const SharedPtr<IValueCollection>   m_config;
        IHost* const                        m_host;
        Crypto::Ed25519Key                  m_identity;
        std::string                         m_deviceId;
        std::string                         m_pi;
        std::string                         m_psi;

        std::mutex                          m_mtxClock;
        std::shared_ptr<PtpClock>           m_clock;
    };
}
