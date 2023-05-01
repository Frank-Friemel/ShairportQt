
#ifdef _WIN32
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#endif
#include <fcntl.h>
#include <exception>
#include <stdexcept>
#include <assert.h>

#include "Networking.h"

using namespace std;
using namespace literals;

namespace Networking
{
	void Init()
	{
#ifdef _WIN32
		WSADATA wsaData{};

		// Initialize Winsock
		const int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
		
		if (result != 0)
		{
			throw runtime_error("WSAStartup failed");
		}
#endif
	}

    int CreateSocket(bool stream /*= true*/, bool bV4 /*= true*/) noexcept
    {
        return ::socket(bV4 ? AF_INET : AF_INET6, stream ? SOCK_STREAM : SOCK_DGRAM, 0);
    }

    void DestroySocket(int sd) noexcept
    {
    #ifdef _WIN32
        if (0 != ::closesocket(sd))
        {
            assert(false);
        }
    #else
        if (0 != ::close(sd))
        {
            assert(false);
        }
    #endif
    }

    bool SetSocketBlockingEnabled(int sd, bool blocking) noexcept
    {
        if (sd < 0)
        {
            return false;
        }
#ifdef _WIN32
        unsigned long mode = blocking ? 0 : 1;
        return ::ioctlsocket(sd, FIONBIO, &mode) == 0;
#else
        int flags = ::fcntl(sd, F_GETFL, 0);

        if (flags == -1)
        {
            return false;
        }
        flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
        return ::fcntl(sd, F_SETFL, flags) == 0;
#endif
    }

    int WaitForIncomingData(int sd, unsigned int ms /*= 0xffffffff*/) noexcept
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sd, &read_fds);

        if (ms == 0xffffffff)
        {
            return ::select(sd + 1, &read_fds, NULL, NULL, NULL);
        }
        struct timeval tv{};

        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms - (tv.tv_sec * 1000)) * 1000;

        return ::select(sd + 1, &read_fds, NULL, NULL, &tv);
    }

    int Read(int sd, unsigned char* buffer, unsigned int size) noexcept
    {
        return ::recv(sd,
#ifdef _WIN32
            (char*)buffer, static_cast<int>(size),
#else
            buffer, size,
#endif
            0);
    }

    std::string GetPeerIP(int sd) noexcept
    {
        std::string result;

        struct sockaddr_in6	laddr_in;
        socklen_t			laddr_size = (socklen_t)sizeof(laddr_in);

        if (0 == ::getpeername(sd, (sockaddr*)&laddr_in, &laddr_size))
        {
            char buf[NI_MAXHOST];

            memset(buf, 0, sizeof(buf));

            if (0 == ::getnameinfo((const sockaddr*)&laddr_in, laddr_size, buf, NI_MAXHOST, NULL, 0, NI_NUMERICHOST))
            {
                try
                {
                    result = buf;
                }
                catch (...)
                {
                }
            }
        }
        return result;
    }
} // namespace Networking