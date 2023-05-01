#include "RaopServer.h"
#include "base64.h"
#include "crypto.h"
#include "Trim.h"
#include "HairTunes.h"
#include <spdlog/spdlog.h>
#include "libutils.h"
#include "definitions.h"
#include <stdlib.h>

#define CPPHTTPLIB_THREAD_POOL_COUNT 1
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib/httplib_raop.h"

#include <future>
#include "dnssd.h"

using namespace std;
using namespace string_literals;
using namespace chrono_literals;

static bool DigestOk(const httplib::Request& request, const string& password);

RaopServer::RaopServer(SharedPtr<IValueCollection> config, SharedPtr<DnsSD> dnsSD, IRaopEvents* raopEvents /*= nullptr*/)
	: m_srvHttp{ make_unique<httplib::Server>() }
	, m_serviceDisabled{ false }
	, m_config{ config }
	, m_dnsSD{ move(dnsSD) }
	, m_rsa{ make_unique<Crypto::Rsa>() }
	, m_clients{ MakeShared<ValueCollection>() }
	, m_raopEvents{ raopEvents }
	, m_metaInfo{ !VariantValue::Key("NoMetaInfo").TryGet<bool>(config).value_or(false) ||
					VariantValue::Key("ForceMetaInfo").TryGet<bool>(config).value_or(false) }
{
	assert(m_config.IsValid());
	assert(m_clients.IsValid());
	assert(m_dnsSD.IsValid());

	char buf[256]{};
	DWORD sz = sizeof(buf);
	
	if (::GetComputerNameA(buf, &sz))
	{
		m_hostName = string(buf, sz);
	}
	else
	{
		assert(false);
	}
	spdlog::info("hostname: {}", m_hostName);

	// create the client-collection
	VariantValue::Key("RaopClients").Set(m_config, m_clients);
	m_httpServerThread = make_unique<thread>([this]() { Run(); });
}

RaopServer::~RaopServer()
{
	m_srvHttp->stop();

	if (m_httpServerThread->joinable())
	{
		try
		{
			m_httpServerThread->join();
		}
		catch (...)
		{
			assert(false);
		}
	}
	// destroy the client-collection
	VariantValue::Key("RaopClients").Remove(m_config);
}

SharedPtr<IValueCollection> RaopServer::GetClient(const std::string& remoteAddr)
{
	return GetClient(remoteAddr, false);
}

SharedPtr<IValueCollection> RaopServer::GetClient(const string& remoteAddr, bool create)
{
	// try to get client from the client-collection by remote address
	auto client = VariantValue::Key(remoteAddr).TryGet<SharedPtr<IValueCollection>>(m_clients);

	// create a new client in case it doesn't exist yet
	if (!client.has_value() && create)
	{
		const VariantValue::Key id("ID");

		// create a client object where the ID is the "remote address"
		client = MakeShared<ValueCollection>(ValueCollection::ValueType{ id , remoteAddr });
		assert(id.Get<string>(client.value()) == remoteAddr);
		
		// insert new client into our collection
		VariantValue::Key(remoteAddr).Set(m_clients, client.value());
	}
	return move(client.value());
}

void RaopServer::RemoveClient(const string& remoteAddr) noexcept
{
	// remove client from collection
	m_clients->Remove(VariantValue::Key(remoteAddr));
}

bool RaopServer::IsPlaying() const noexcept
{
	const shared_lock<shared_mutex> guard(m_mtxDecoder);

	if (m_decoder)
	{
		return m_decoder->IsPlaying();
	}
	return false;
}

bool RaopServer::GetProgress(int& duration, int& position, string& clientID) const noexcept
{
	const shared_lock<shared_mutex> guard(m_mtxDecoder);

	if (m_decoder)
	{
		try
		{
			clientID = m_decoder->GetClientID();
		}
		catch (...)
		{
		}

		duration = m_duration;
		position = m_position + m_decoder->GetProgressTime();
		return true;
	}
	return false;
}

bool RaopServer::EnableServer(bool enable) noexcept
{
	m_serviceDisabled = !enable;
	return !!m_decoder;
}

void RaopServer::Run() noexcept
{
	try
	{
		// keep alive forever (lifetime is being controled by client)
		m_srvHttp->set_keep_alive_max_count(numeric_limits<size_t>::max());

		// setup "get" handler for http
		m_srvHttp->Get(".+", [&](const httplib::Request& request, httplib::Response& response)
			{
				try
				{
					response.version = request.version;

					response.set_header("CSeq"s, request.get_header_value("CSeq"s));
					response.set_header("Audio-Jack-Status"s, "connected; type=analog"s);
					response.set_header("Server"s, "AirTunes/105.1"s);

					if (request.has_header("Apple-Challenge"s))
					{
						const string strChallenge = request.get_header_value("Apple-Challenge"s);

						const auto challenge = Base64::Decode(strChallenge);

						if (!challenge.empty())
						{
							const auto hwAddr = VariantValue::Key("HWaddress").Get<vector<uint8_t>>(m_config);

							auto appleResponse = MakeShared<BlobStream>();

							appleResponse->Write(challenge.data(), static_cast<ULONG>(challenge.size()), nullptr);
							appleResponse->Write(request.localIPAddr.data(), static_cast<ULONG>(request.localIPAddr.size()), nullptr);
							appleResponse->Write(hwAddr.data(), static_cast<ULONG>(hwAddr.size()), nullptr);
							const size_t size = appleResponse->GetSize();

							assert(size <= 64);

							if (size < 32)
							{
								appleResponse->SetSize(ToULargeInteger(32));
							}
							auto strAppleResponse = Base64::Encode(m_rsa->Sign(appleResponse->GetBuffer()));

							TrimRight(strAppleResponse, "=\r\n"s);

							response.set_header("Apple-Response"s, strAppleResponse);
						}
					}
					auto hasPassword = VariantValue::Key("HasPassword").TryGet<bool>(m_config);

					if (hasPassword.has_value() && hasPassword.value())
					{
						auto password = VariantValue::Key("Password").TryGet<string>(m_config);

						if (password.has_value())
						{
							const auto client = GetClient(request.remote_addr, true);
							auto nonceDigest = VariantValue::Key("Nonce").TryGet<string>(client);

							if (!nonceDigest.has_value() || !DigestOk(request, password.value()))
							{
								if (!nonceDigest.has_value())
								{
									char nonce[20];

									for (long i = 0; i < 20; ++i)
									{
										nonce[i] = 'A' + static_cast<char>(CreateRand(26));
									}
									string strNonceDigest = httplib::detail::MD5_EX({ nonce, 20 }, true);

									VariantValue::Key("Nonce").Set(client, strNonceDigest);
									nonceDigest = move(strNonceDigest);
								}
								const auto wwwAuthenticate = "Digest realm=\""s
									+ VariantValue::Key("APname").Get<string>(m_config)
									+ "\", nonce=\""s
									+ nonceDigest.value()
									+ "\""s;

								response.set_header("WWW-Authenticate"s, wwwAuthenticate);
								response.status = 401; // "Unauthorized"
								return;
							}
						}
					}
					if (request.method == "OPTIONS"s)
					{
						response.set_header("Public"s, "ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER, POST, GET"s);
					}
					else if (request.method == "GET_PARAMETER"s)
					{
						if (request.body.find("olume"s) != string::npos)
						{
							const Variant volume = VariantValue::Key("Volume").Get<double>(m_config) / 1000.;
							string strVolume = VariantValue::Get<string>(volume);

							if (GetFloatingPointSeparator() != '.')
							{
								const auto fp = strVolume.find(GetFloatingPointSeparator());

								if (fp != string::npos)
								{
									strVolume[fp] = '.';
								}
							}
							response.set_content("volume: "s + strVolume + "\r\n"s, "text/parameters"s);
						}
					}
					else if (request.method == "SET_PARAMETER"s)
					{
						// we need a content-type
						if (request.has_header("content-type"s))
						{
							// unify the content-type to lowercase characters
							const auto contentType = ToLower(request.get_header_value("content-type"s));

							// now, check several content-types which we support...
							if (contentType == "application/x-dmap-tagged"s)
							{
								// title info (album, track and artist)
								// https://openairplay.github.io/airplay-spec/audio/metadata.html
								if (!request.body.empty() && m_raopEvents)
								{
									DmapInfo dmapInfo;

									try
									{
										// extract title info from request-body
										if (0 == dmap_parse(&dmapInfo, request.body.c_str(), request.body.length()))
										{
											// send title info to callback receiver
											m_raopEvents->OnSetCurrentDmapInfo(move(dmapInfo));
										}
									}
									catch (const exception& e)
									{
										spdlog::error("exception while parsing dmap: {}", e.what());
									}
								}
							}
							else if (contentType == "text/parameters"s)
							{
								// progress- and/or volume-information
								if (!request.body.empty())
								{
									map<string, string, httplib::detail::ci> mapKeyValue;

									ParseRegEx(request.body, "[^[:space:]:]+[:space:]*:[^\r\n]+[\r\n]*"s
										, [&mapKeyValue](string s) -> bool
										{
											string strLeft;
											string strRight;

											if (!ParseRegEx(s, "[^[:space:]:]+"s, [&strLeft](string t) -> bool
												{
													strLeft = move(t);
													return false;
												}))
											{
												return false;
											}
											if (!ParseRegEx(s, ":[^\r\n]+"s, [&strRight](string t) -> bool
												{
													Trim(t, " \t\r\n:"s);
													strRight = move(t);
													return false;
												}))
											{
												return false;
											}
											mapKeyValue[strLeft] = move(strRight);
											return true;
										});

									if (!mapKeyValue.empty())
									{
										auto i = mapKeyValue.find("volume"s);

										if (i != mapKeyValue.end())
										{
											const Variant varVolume = i->second;
											const int64_t volume = static_cast<uint64_t>(VariantValue::Get<double>(varVolume) * 1000.);

											VariantValue::Key("Volume").Set(m_config, volume);
										}
										else
										{
											i = mapKeyValue.find("progress"s);

											if (i != mapKeyValue.end())
											{
												vector<uint64_t> listProgressValues;
												listProgressValues.reserve(3);

												ParseRegEx(i->second, "[\\d]+"s, [&listProgressValues](const string& s) -> bool
													{
														listProgressValues.emplace_back(std::strtoull(s.c_str(), nullptr, 10));
														return true;
													});

												if (listProgressValues.size() >= 3)
												{
													uint64_t start = listProgressValues[0];
													uint64_t curr = listProgressValues[1];
													uint64_t end = listProgressValues[2];

													if (end >= start)
													{
														// obviously set wrongly by some Apps (e.g. Soundcloud) 
														if (start > curr)
														{
															std::swap(start, curr);
														}
														if (curr <= end)
														{
															const lock_guard<shared_mutex> guard(m_mtxDecoder);

															if (m_decoder)
															{
																const uint64_t samplingFreq = m_decoder->GetSamplingFreq();

																m_duration = (int)((end - start) / samplingFreq);
																m_position = (int)((curr - start) / samplingFreq);

																m_decoder->ResetProgess();
															}
														}
													}
												}
											}
										}
									}
								}
							}
							else if (contentType.length() > 6 && contentType.substr(0, 6) == "image/"s)
							{
								// the content is an image
								if (m_raopEvents)
								{
									string imageType = ToUpper(contentType.substr(6));

									if (imageType == "NONE"s)
									{
										// clear current image
										m_raopEvents->OnSetCurrentImage(nullptr, 0, move(imageType));
									}
									else if (imageType == "JPEG"s || imageType == "PNG"s)
									{
										const size_t bodyLength = request.body.length();

										if (bodyLength)
										{
											// send image data to callback receiver
											m_raopEvents->OnSetCurrentImage(request.body.data(), bodyLength, move(imageType));
										}
									}
									else
									{
										spdlog::info("unknown image type: {}", imageType);
									}
								}
							}
							else
							{
								spdlog::debug("unknown content-type: {}", contentType);
							}
						}
					}
					else if (request.method == "ANNOUNCE"s)
					{
						map<string, string, httplib::detail::ci> mapKeyValue;

						ParseRegEx(request.body, "a[:space:]*=[:space:]*[^[:space:]:]+[:space:]*:[^\r\n]+[\r\n]+"s
							, [&mapKeyValue](string s) -> bool
							{
								string strLeft;
								string strRight;

								const auto eq = s.find('=');

								if (eq == string::npos)
								{
									return false;
								}
								const auto item = s.substr(eq + 1);

								if (!ParseRegEx(item, "[^[:space:]:]+"s, [&strLeft](string t) -> bool
									{
										strLeft = move(t);
										return false;
									}))
								{
									return false;
								}
								if (!ParseRegEx(item, ":[^\r\n]+"s, [&strRight](string t) -> bool
									{
										Trim(t, " \t\r\n:"s);
										strRight = move(t);
										return false;
									}))
								{
									return false;
								}
								mapKeyValue[strLeft] = move(strRight);
								return true;
							});

						string clientInfo;
						ParseRegEx(request.body, "i[:space:]*=[^\r\n]+"s
							, [&clientInfo](string s) -> bool
							{
								const auto eq = s.find('=');

								if (eq != string::npos)
								{
									clientInfo = s.substr(eq + 1);
									Trim(clientInfo, " \t\r\n"s);
								}
								return false;
							});

						auto client = GetClient(request.remote_addr, true);

						if (!clientInfo.empty())
						{
							VariantValue::Key("clientInfo").Set(client, clientInfo);
						}
						else
						{
							VariantValue::Key("clientInfo").Set(client, MakeShared<FutureValue>(
								async(launch::async, [](const string ipAddr) -> Variant {

									return httplib::detail::IPToName(ipAddr);

									}, request.remote_addr)));
						}
						VariantValue::Key("fmtp").Set(client, mapKeyValue["fmtp"s]);
						VariantValue::Key("aesiv").Set(client, Base64::Decode(mapKeyValue["aesiv"s]));
						VariantValue::Key("rsaaeskey").Set(client, m_rsa->Decrypt(Base64::Decode(mapKeyValue["rsaaeskey"s])));

						if (request.has_header("Active-Remote"s))
						{
							VariantValue::Key("Active-Remote").Set(client, request.get_header_value("Active-Remote"s));
						}
						if (request.has_header("DACP-ID"s))
						{
							VariantValue::Key("DACP-ID").Set(client, request.get_header_value("DACP-ID"s));
						}
					}
					else if (request.method == "SETUP"s)
					{
						auto client = GetClient(request.remote_addr);

						if (client.IsValid())
						{
							const string transportRequested = request.get_header_value("transport"s);

							ParseRegEx(transportRequested, "control_port=[\\d]+"s, [&](string s) -> bool
								{
									VariantValue::Key("control_port").Set(client, stoi(s.substr(strlen("control_port="))));
									return false;
								});
							ParseRegEx(transportRequested, "timing_port=[\\d]+"s, [&](string s) -> bool
								{
									VariantValue::Key("timing_port").Set(client, stoi(s.substr(strlen("timing_port="))));
									return false;
								});

							if (m_serviceDisabled)
							{
								response.status = 503; // Service Unavailable
								return;
							}

							if (m_raopEvents)
							{
								auto dacpID = request.get_header_value("DACP-ID"s);

								if (!dacpID.empty())
								{
									const auto activeRemote = request.get_header_value("Active-Remote"s);
									DacpID currentDacpID{ HexToInteger(dacpID), m_hostName, request.remote_addr, activeRemote, 0 };

									m_raopEvents->OnSetCurrentDacpID(move(currentDacpID));
								}
								else
								{
									spdlog::debug("no DACP-ID");
								}
							}
							const lock_guard<shared_mutex> guard(m_mtxDecoder);

							m_decoder.reset();

							try
							{
								m_decoder = make_unique<HairTunes>(m_config, move(client));

								const unsigned int serverPort = m_decoder->GetServerPort();
								const unsigned int controlPort = m_decoder->GetControlPort();
								const unsigned int timingPort = m_decoder->GetTimingPort();

								const string transportResponse =
									"RTP/AVP/UDP;unicast;mode=record;server_port="s +
									to_string(serverPort) +
									";control_port="s +
									to_string(controlPort) +
									";timing_port="s +
									to_string(timingPort);

								response.set_header("Transport"s, transportResponse);
								response.set_header("Session"s, "DEADBEEF"s);
							}
							catch (...)
							{
								spdlog::error("failed to setup decoder");
								response.status = 503; // Service Unavailable

								// cleanup
								m_decoder.reset();
							}
						}
					}
					else if (request.method == "FLUSH"s)
					{
						response.set_header("RTP-Info"s, "rtptime=0"s);
					}
					else if (request.method == "TEARDOWN"s)
					{
						RemoveClient(request.remote_addr);
						response.set_header("Connection"s, "close"s);
						spdlog::debug("client {} torn down", request.remote_addr);

						unique_ptr<HairTunes> decoder;
						{
							const lock_guard<shared_mutex> guard(m_mtxDecoder);

							if (m_decoder && m_decoder->GetClientID() == request.remote_addr)
							{
								decoder.swap(m_decoder);
							}
						}
						if (decoder)
						{
							decoder->Flush();
							decoder.reset();
						}
					}
					else if (request.method == "RECORD"s)
					{
						response.set_header("Audio-Latency"s, "6174"s);
					}
				}
				catch (...)
				{
					spdlog::error("exception in rtp request handler");
					assert(false);
				}
			});

		spdlog::info("Starting Raop Http Server");

		int port = 5000;

#ifdef _WIN32
		string legacyApName;
		GetValueFromRegistry(HKEY_CURRENT_USER, "ApName", legacyApName, "Software\\Shairport4w");

		if (!legacyApName.empty())
		{
			port += 10;
		}
#endif
		// try several ports until we find one to listen to
		for (; port <= 6000; ++port)
		{
			promise<bool> serverFailed;

			// since our http-service is blocking
			// we're publishing the Raop Service asynchronously
			const auto publishRaopService = async(launch::async, [this](future<bool>&& serverFailed, int port) 
			{
				DnsHandlePtr dnsSDHandle;
				
				// the future will timeout if the http service manages to start successfully
				if (serverFailed.wait_for(500ms) == future_status::timeout)
				{
					spdlog::info("Publishing Raop Service on Port {}", port);
					VariantValue::Key("RaopPort").Set(m_config, port);

					// Bonjour will publish the service
					dnsSDHandle = m_dnsSD->CreateRaopServiceFromConfig(m_config, m_metaInfo);

					// check the result
					if (!dnsSDHandle->Succeeded())
					{
						// retry in case Bonjour isn't up yet
						long nTry = 10;

						do
						{
							if (serverFailed.wait_for(500ms) == future_status::ready)
							{
								return dnsSDHandle;
							}
							dnsSDHandle = m_dnsSD->CreateRaopServiceFromConfig(m_config, m_metaInfo);
						} while (!dnsSDHandle->Succeeded() && nTry-- > 0);
					}
					const bool bSuccess = dnsSDHandle->Succeeded();

					if (m_raopEvents)
					{
						// notify the event sink
						m_raopEvents->OnCreateRaopService(bSuccess);
					}
					
					if (bSuccess)
					{
						spdlog::debug("Succeeded to publish RAOP Service with name \"{}\"", VariantValue::Key("APname").Get<string>(m_config));
					}
					else
					{
						spdlog::error("*Failed* to publish RAOP Service \"{}\" with code {}",
							VariantValue::Key("APname").Get<string>(m_config), dnsSDHandle->ErrorCode());
					}
				}
				return dnsSDHandle;
			}, serverFailed.get_future(), port);

			// listen for incoming http-requests
			// blocks, in case the port can be aquired by us
			if (!m_srvHttp->listen("0.0.0.0", port))
			{
				// signal the async publisher, that we've failed
				serverFailed.set_value(true);
				spdlog::debug("Could not start RaopServer on port {}", port);

				// try next port in row
				continue;
			}
			serverFailed.set_value(false);
			break;
		}
		spdlog::info("Stopped RaopServer");
	}
	catch(...)
	{
		spdlog::error("Failed to start RaopServer");
	}
	unique_ptr<HairTunes> decoder;

	if (m_decoder)
	{
		const lock_guard<shared_mutex> guard(m_mtxDecoder);
		decoder.swap(m_decoder);
	}
	if (decoder)
	{
		decoder->Flush();
		decoder.reset();
	}
}

static bool DigestOk(const httplib::Request& request, const string& password)
{
	const string strAuth = request.get_header_value("Authorization"s);

	if (strAuth.find("Digest ") == 0 || strAuth.find("digest ") == 0)
	{
		map<string, string> mapAuth;

		ParseRegEx(strAuth.c_str() + 7, "[^\\s=]+[\\s]*=[\\s]*\"[^\"]+\""s
			, [&mapAuth](string s) -> bool
			{
				string strLeft;
				string strRight;

				if (!ParseRegEx(s, "[^\\s\"=]+"s, [&strLeft](string t) -> bool
					{
						strLeft = move(t);
						return false;
					}))
				{
					return false;
				}
				if (!ParseRegEx(s, "\"[^\"]+\""s, [&strRight](string t) -> bool
					{
						Trim(t, "\""s);
						strRight = move(t);
						return false;
					}))
				{
					return false;
				}
				mapAuth.emplace(ToLower(move(strLeft)), move(strRight));
				return true;
			});

		const string str1 = mapAuth["username"s] + ":"s + mapAuth["realm"s] + ":"s + password;
		const string str2 = request.method + ":"s + mapAuth["uri"s];

		const string strDigest1 = httplib::detail::MD5_EX(str1, true);
		const string strDigest2 = httplib::detail::MD5_EX(str2, true);

		const string strDigest = httplib::detail::MD5_EX(strDigest1 + ":"s + mapAuth["nonce"s] + ":"s + strDigest2, true);

		const auto response = mapAuth["response"s];

		if (strDigest == response)
		{
			return true;
		}
	}
	return false;
}

int RaopServer::SendDacpCommand(const DacpID& dacpID, const string& cmd) noexcept
{
	int result = 0;

	try
	{
		httplib::Client client(dacpID.remoteIP, SWAP16(dacpID.port));
		httplib::Headers headers;

		headers.emplace("Host"s, dacpID.hostName + ".local."s);
		headers.emplace("Active-Remote"s, dacpID.activeRemote);

		auto response = client.Get("/ctrl-int/1/"s + cmd, headers);

		if (response.error() == httplib::Error::Success)
		{
			result = response.value().status;
		}
		else
		{
			result = -1;
		}
	}
	catch (const exception& e)
	{
		spdlog::error("failed to SendDacpCommand({}): {}", cmd, e.what());
	}
	return result;
}

// Dmap Parser Callbacks
void RaopServer::on_string(void* ctx, const char*, const char* name, const char* buf, size_t len)
{
	if (name && buf && len)
	{
		assert(ctx);
		DmapInfo* dmapInfo = static_cast<DmapInfo*>(ctx);

		if (strcmp(name, "daap.songalbum") == 0)
		{
			dmapInfo->album = string(buf, len);
		}
		else if (strcmp(name, "daap.songartist") == 0)
		{
			dmapInfo->artist = string(buf, len);
		}
		else if (strcmp(name, "dmap.itemname") == 0)
		{
			dmapInfo->track = string(buf, len);
		}
	}
}