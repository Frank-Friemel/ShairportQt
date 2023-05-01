#pragma once

#include <thread>
#include <memory>
#include <mutex>
#include "LayerCake.h"
#include "DmapParser.h"

typedef struct structDacpID
{
	uint64_t		id{ 0 };
	std::string		hostName;
	std::string		remoteIP;
	std::string		activeRemote;
	uint16_t		port{ 0 };
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
class HairTunes;

class RaopServer
	: protected DmapParser
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

protected:
	// Dmap Parser Callbacks
	void on_string(void* ctx, const char* code, const char* name, const char* buf, size_t len) override;

private:
	SharedPtr<IValueCollection> GetClient(const std::string& remoteAddr, bool create);
	void RemoveClient(const std::string& remoteAddr) noexcept;

public:
	const bool								m_metaInfo;

private:
	const std::unique_ptr<httplib::Server> 	m_srvHttp;
	std::string								m_hostName;
	std::unique_ptr<std::thread> 			m_httpServerThread;
	const SharedPtr<IValueCollection>  		m_config;
	const SharedPtr<DnsSD> 					m_dnsSD;
	const std::unique_ptr<Crypto::Rsa> 		m_rsa;
	std::unique_ptr<HairTunes>				m_decoder;
	mutable std::shared_mutex				m_mtxDecoder;
	std::atomic_bool						m_serviceDisabled;
	const SharedPtr<IValueCollection>  		m_clients;
	IRaopEvents* const						m_raopEvents;
	int										m_duration{ 0 }; // total duration time [s]
	int										m_position{ 0 }; // current play position time [s]
};
