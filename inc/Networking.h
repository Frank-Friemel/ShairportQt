#pragma once

#include <string>

namespace Networking
{
	void	Init();
	int		CreateSocket(bool stream = true, bool bV4 = true) noexcept;
	void	DestroySocket(int sd) noexcept;
	bool	SetSocketBlockingEnabled(int sd, bool blocking) noexcept;
	int		WaitForIncomingData(int sd, unsigned int ms = 0xffffffff) noexcept;
	int		Read(int sd, unsigned char* buffer, unsigned int size) noexcept;
	std::string GetPeerIP(int sd) noexcept;

} // namespace Networking