#include "RaopEndpoint.h"
#include "sockpp/udp_socket.h"
#include "sockpp/udp6_socket.h"
#include "sockpp/tcp_acceptor.h"
#include "sockpp/tcp6_acceptor.h"
#include "sockpp/inet_address.h"
#include "sockpp/socket.h"
#include "sockpp/sock_address.h"

using namespace std;
using namespace string_literals;

static mutex mtxPacketPool;
static list<unique_ptr<RtpPacket>> packetPool;

void PutPacketToPool(unique_ptr<RtpPacket>&& p)
{
    assert(p);
    p->Init();

    const lock_guard<mutex> guard(mtxPacketPool);
    packetPool.emplace_back(move(p));
}

static unique_ptr<RtpPacket> GetNewPacketFromPool()
{
    {
        const lock_guard<mutex> guard(mtxPacketPool);

        if (!packetPool.empty())
        {
            unique_ptr<RtpPacket> result = move(packetPool.front());
            packetPool.pop_front();
            return result;
        }
    }
    return make_unique<RtpPacket>();
}

static USHORT GetUniquePortNumber()
{
    sockpp::socket_initializer::initialize();
    static atomic_uint16_t src{ 6000 };
    const auto result = src++;

	if (result == 12000)
    {
		src = 6000;
    }
	return result;
}

RtpEndpoint::RtpEndpoint(IRtpRequestHandler* requestHandler, const string& peer)
    : m_requestHandler{ requestHandler }
    , m_peer(peer)
    , m_isV4{ true }
    , m_port { 0 }
    , m_stop{ false }
{
    assert(m_requestHandler);
    assert(!m_peer.empty());

    USHORT port = GetUniquePortNumber();

    try
    {
        const sockpp::inet_address addrPeer(m_peer, 1024);
    }
    catch(...)
    {
        m_isV4 = false;
    }

    for (int i = 0; i < 1024; ++i, port = GetUniquePortNumber())
    {
        unique_ptr<sockpp::sock_address> addr;

        if (m_isV4)
        {
            addr = make_unique<sockpp::inet_address>(port);
            m_socket = make_unique<sockpp::udp_socket>();

        }
        else
        {
            addr = make_unique<sockpp::inet6_address>(port);
            m_socket = make_unique<sockpp::udp6_socket>();
        }

#ifdef _WIN32
        const DWORD timeout = 1000;
        m_socket->set_option(SOL_SOCKET, SO_RCVTIMEO, static_cast<const void*>(& timeout), static_cast<socklen_t>(sizeof(timeout)));
#endif
        if (!m_socket->bind(*addr))
        {
            continue;
        }
        m_port   = port;
        m_thread = make_unique<thread>([this]()
        {
            Run();
        });
        return;
    }
    throw runtime_error("could not acquire port for udp socket");
}

RtpEndpoint::~RtpEndpoint()
{
    m_stop = true;

    if (m_socket)
    {
        try
        {
            m_socket->shutdown();
        }
        catch (...)
        {
            assert(false);
        }
    }
    if (m_thread)
    {
        if (m_thread->joinable())
        {
            try
            {
                m_thread->join();
            }
            catch (...)
            {
                assert(false);
            }
        }
    }
}

void RtpEndpoint::Run() noexcept
{
    try
    {
        while (!m_stop)
        {
            auto packet = GetNewPacketFromPool();
            assert(packet->size() == RAOP_PACKET_MAX_SIZE);
            
            sockpp::sock_address_any addr;
            const auto size = m_socket->recv_from(packet->data(), packet->size(), &addr);

            if (size <= 0)
            {
                PutPacketToPool(move(packet));
                continue;
            }
            packet->resize(size);

            m_requestHandler->OnRequest(this, move(packet));
        }
    }
    catch(...)
    {
    }
}

bool RtpEndpoint::SendTo(const void* buf, size_t len, USHORT port) noexcept
{
    try
    {
        if (m_isV4)
        {
            sockpp::udp_socket sock;

            if (!sock.connect(sockpp::inet_address(m_peer, port))) 
            {
                return FALSE;
            }
            return sock.send(buf, len) == len;
        }
        sockpp::udp6_socket sock;

        if (!sock.connect(sockpp::inet6_address(m_peer, port))) 
        {
            return FALSE;
        }
        return sock.send(buf, len) == len;
    }
    catch(...)
    {
    }
    return false;
}
