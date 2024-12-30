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

void PutPacketToPool(unique_ptr<RtpPacket>&& p) noexcept
{
    assert(p);
    p->Init();

    const lock_guard<mutex> guard(mtxPacketPool);

    try
    {
        packetPool.emplace_back(move(p));
    }
    catch (...)
    {
        p.reset();
    }
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

static uint16_t GetUniquePortNumber()
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

RtpEndpoint::RtpEndpoint(IRtpRequestHandler* requestHandler, const string& peer, const uint16_t peerPort /*= 0*/)
    : m_requestHandler{ requestHandler }
    , m_peer{ peer }
    , m_peerPort{ peerPort }
    , m_isV4{ true }
    , m_port { 0 }
    , m_stop{ false }
{
    assert(m_requestHandler);
    assert(!m_peer.empty());

    // call "GetUniquePortNumber" here - which will invoke "socket_initializer"
    uint16_t port = GetUniquePortNumber();

    try
    {
        const sockpp::inet_address addrPeer(m_peer, m_peerPort ? m_peerPort : static_cast<uint16_t>(1024));

        try
        {
            m_peerAddress = make_unique<sockpp::inet_address>(addrPeer);

            if (m_peerPort)
            {
                m_peerSendToSocket = make_unique<sockpp::udp_socket>();
            }
        }
        catch (...)
        {
            throw;
        }
    }
    catch(...)
    {
        m_isV4 = false;
        m_peerAddress = make_unique<sockpp::inet6_address>(m_peer, m_peerPort ? m_peerPort : static_cast<uint16_t>(1024));

        if (m_peerPort)
        {
            m_peerSendToSocket = make_unique<sockpp::udp6_socket>();
        }
    }

    if (m_peerSendToSocket)
    {
        try
        {
            if (!m_peerSendToSocket->connect(*m_peerAddress))
            {
                // unexpected because this is not really a "connection" attempt
                assert(false);
                m_peerSendToSocket.reset();
            }
        }
        catch (...)
        {
            // unexpected because this is not really a "connection" attempt
            assert(false);
            m_peerSendToSocket.reset();
        }
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

bool RtpEndpoint::SendTo(const void* buf, size_t len, uint16_t port) noexcept
{
    if (port == m_peerPort)
    {
        try
        {
            const lock_guard<mutex> guard(m_mtxSendToSocket);

            if (m_peerSendToSocket)
            {
                if (m_peerSendToSocket->send(buf, len) == len)
                {
                    return true;
                }
            }
        }
        catch (...)
        {
        }
    }
    try
    {
        if (m_isV4)
        {
            sockpp::udp_socket sock;

            if (port == m_peerPort)
            {
                assert(m_peerAddress);

                if (!sock.connect(*m_peerAddress))
                {
                    return false;
                }
            }
            else if (!sock.connect(sockpp::inet_address(m_peer, port))) 
            {
                return false;
            }
            return sock.send(buf, len) == len;
        }
        sockpp::udp6_socket sock;

        if (port == m_peerPort)
        {
            assert(m_peerAddress);

            if (!sock.connect(*m_peerAddress))
            {
                return false;
            }
        }
        else if (!sock.connect(sockpp::inet6_address(m_peer, port)))
        {
            return false;
        }
        return sock.send(buf, len) == len;
    }
    catch(...)
    {
    }
    return false;
}
