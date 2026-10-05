#pragma once

#include <thread>
#include <memory>
#include <mutex>
#include "LayerCake.h"
#include "DmapParser.h"
#include "airplay2/AirPlay2Handler.h"

typedef struct structDacpID
{
	uint64_t		id{ 0 };
	std::string		hostName;
	std::string		remoteIP;
	std::string		activeRemote;
	uint16_t		port{ 0 }; // network order
} DacpID;

typedef struct structDmapInfo
{
	std::string		album;
	std::string		track;
	std::string		artist;
} DmapInfo;

class IRaopEvents
{
public:
	virtual void OnCreateRaopService(bool success) noexcept = 0;
	virtual void OnSetCurrentDacpID(DacpID&& dacpID) noexcept = 0;
	virtual void OnSetCurrentDmapInfo(DmapInfo&& dmapInfo) noexcept = 0;
	virtual void OnSetCurrentImage(const char* data, size_t dataLen, std::string&& imageType) noexcept = 0;
};

namespace httplib
{
	class Server;
}

namespace Crypto
{
	class Rsa;
}

class DnsSD;
class IAudioSession;

class RaopServer
	: protected DmapParser
	, public AirPlay2::IHost
{
	class HttpServer;

private:
	void Run() noexcept;

public:
	RaopServer(const SharedPtr<IValueCollection> config, SharedPtr<DnsSD> dnsSD, IRaopEvents* raopEvents = nullptr);
	~RaopServer();

	RaopServer(const RaopServer&) = delete;
	RaopServer& operator=(const RaopServer&) = delete;

	bool EnableServer(bool enable) noexcept;

	bool GetProgress(int& duration, int& position, std::string& clientID) const noexcept;
	bool IsPlaying() const noexcept;

	static int SendDacpCommand(const DacpID& dacpID, const std::string& cmd) noexcept;

	SharedPtr<IValueCollection> GetClient(const std::string& remoteAddr);

	uint32_t GetErrorCode() const noexcept;
	
protected:
	// Dmap Parser Callbacks
	void on_string(void* ctx, const char* code, const char* name, const char* buf, size_t len) override;

	// AirPlay 2 host callbacks
	void OnSessionStarted(const std::shared_ptr<AirPlay2::Ap2AudioSession>& session) override;
	void OnSessionEnded(const std::shared_ptr<AirPlay2::Ap2AudioSession>& session) override;
	void OnDacp(const std::string& dacpID, const std::string& activeRemote, const std::string& remoteAddr) override;

private:
	SharedPtr<IValueCollection> GetClient(const std::string& remoteAddr, bool create);
	bool RemoveClient(const std::string& remoteAddr) noexcept;

public:
	const bool								m_metaInfo;

private:
	const std::unique_ptr<httplib::Server> 	m_srvHttp;
	std::string								m_hostName;
	std::unique_ptr<std::thread> 			m_httpServerThread;
	const SharedPtr<IValueCollection>  		m_config;
	const SharedPtr<DnsSD> 					m_dnsSD;
	const std::unique_ptr<Crypto::Rsa> 		m_rsa;
	std::shared_ptr<IAudioSession>			m_decoder;
	mutable std::shared_mutex				m_mtxDecoder;
	std::mutex								m_mtxRequest;	// serializes the RTSP request handling
	const std::unique_ptr<AirPlay2::Service> m_airPlay2;
	std::string								m_lastDacp;
	std::atomic_bool						m_serviceDisabled;
	const SharedPtr<IValueCollection>  		m_clients;
	IRaopEvents* const						m_raopEvents;
	int										m_duration{ 0 }; // total duration time [s]
	int										m_position{ 0 }; // current play position time [s]
	std::atomic_uint32_t					m_errorState{ 0 };
};
