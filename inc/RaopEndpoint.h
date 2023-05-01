#pragma once

#include <stdint.h>
#include <vector>
#include <thread>
#include <atomic>
#include "LayerCake.h"

#define RTP_BASE_HEADER_SIZE			0x04
#define RTP_DATA_HEADER_SIZE			0x08
#define RTP_DATA_OFFSET					0x0C // RTP_BASE_HEADER_SIZE + RTP_DATA_HEADER_SIZE

#define EXTENSION_MASK					0x10
#define MARKER_MASK						0x80
#define PAYLOAD_MASK					0x7F

#define PAYLOAD_TYPE_STREAM_DATA		0x60
#define PAYLOAD_TYPE_STREAM_SYNC		0x54
#define PAYLOAD_TYPE_TIMING_REQUEST		0x52
#define PAYLOAD_TYPE_TIMING_RESPONSE	0x53
#define PAYLOAD_TYPE_RESEND_REQUEST		0x55
#define PAYLOAD_TYPE_RESEND_RESPONSE	0x56

// maximum number of bytes in an RAOP audio data packet including headers
#define RAOP_PACKET_MAX_SIZE			2048

namespace sockpp
{
    class datagram_socket;
}

#ifdef _WIN32
#pragma comment(lib, "sockpp-static.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

class RtpPacket
{
public:
    RtpPacket() noexcept
    {
        Init();
    }

	inline void Init() noexcept
	{
		assert(RAOP_PACKET_MAX_SIZE <= sizeof(buffer));
		memset(buffer, 0, sizeof(buffer));
		buffer[0]	= 0x80;
		bufSize		= RAOP_PACKET_MAX_SIZE;
	}
	inline bool getExtension() const noexcept
	{
		return ((buffer[0] & EXTENSION_MASK) != 0);
	}
	inline void setExtension(const bool value = true) noexcept
	{
		buffer[0] = (value ? (buffer[0] | EXTENSION_MASK) : (buffer[0] & ~EXTENSION_MASK));
	}
	inline bool getMarker() const noexcept
	{
		return ((buffer[1] & MARKER_MASK) != 0);
	}
	inline void setMarker(const bool value = true) noexcept
	{
		buffer[1] = (value ? (buffer[1] | MARKER_MASK) : (buffer[1] & ~MARKER_MASK));
	}
	inline uint8_t getPayloadType() const noexcept
	{
		return (buffer[1] & PAYLOAD_MASK);
	}
	inline void setPayloadType(const uint8_t value) noexcept
	{
		buffer[1] = (buffer[1] & ~PAYLOAD_MASK) | (value & PAYLOAD_MASK);
	}
	inline USHORT getSeqNo() const noexcept
	{
		return SWAP16(*(unsigned short *)(&buffer[2]));
	}
	inline void setSeqNo(USHORT n) noexcept
	{
		*(unsigned short *)(&buffer[2]) = SWAP16(n);
	}
	inline ULONG getTimeStamp() const noexcept
	{
		return SWAP32(*(unsigned long *)(&buffer[4]));
	}
	inline unsigned char* getData() noexcept
	{
		return buffer + RTP_DATA_OFFSET;
	}
	inline const unsigned char* getData() const noexcept
	{
		return buffer + RTP_DATA_OFFSET;
	}
	inline int getDataLen() const noexcept
	{
		return static_cast<int>(size()) - RTP_DATA_OFFSET;
	}
	inline uint32_t getSSRC() const noexcept
	{
		return SWAP32(*(uint32_t *)&buffer[8]);
	}
	inline ULONGLONG getNTPTimeStamp(int nOffset) const noexcept
	{
		return MAKEUINT64(	MAKEUINT32(MAKEUINT16(buffer[nOffset+7], buffer[nOffset+6]), MAKEUINT16(buffer[nOffset+5], buffer[nOffset+4])),
							MAKEUINT32(MAKEUINT16(buffer[nOffset+3], buffer[nOffset+2]), MAKEUINT16(buffer[nOffset+1], buffer[nOffset]))
						);
	}
	inline ULONGLONG getReferenceTime() const noexcept
	{
		return getNTPTimeStamp(8);
	}
	inline ULONGLONG getReceivedTime() const noexcept
	{
		return getNTPTimeStamp(16);
	}
	inline ULONGLONG getSendTime() const noexcept
	{
		return getNTPTimeStamp(24);
	}
	inline void setNTPTimeStamp(int nOffset, ULONGLONG tVal)  noexcept
	{
		const DWORD lDWord = LODWORD(tVal);
		const DWORD hDWord = HIDWORD(tVal);

		const WORD llWord = LOWORD(lDWord);
		const WORD lhWord = HIWORD(lDWord);

		const WORD hlWord = LOWORD(hDWord);
		const WORD hhWord = HIWORD(hDWord);

		buffer[nOffset]		= HIBYTE(hhWord);
		buffer[nOffset+ 1]	= LOBYTE(hhWord);
		buffer[nOffset+ 2]	= HIBYTE(hlWord);
		buffer[nOffset+ 3]	= LOBYTE(hlWord);
		buffer[nOffset+ 4]	= HIBYTE(lhWord);
		buffer[nOffset+ 5]	= LOBYTE(lhWord);
		buffer[nOffset+ 6]	= HIBYTE(llWord);
		buffer[nOffset+ 7]	= LOBYTE(llWord);
	}
	inline void setReferenceTime(ULONGLONG tVal) noexcept
	{
		setNTPTimeStamp(8, tVal);
	}
	inline void setReceivedTime(ULONGLONG tVal) noexcept
	{
		setNTPTimeStamp(16, tVal);
	}
	inline void setSendTime(ULONGLONG tVal) noexcept
	{
		setNTPTimeStamp(24, tVal);
	}
	inline USHORT getMissedSeqNr() const noexcept
	{
		return SWAP16(*(unsigned short*)(&buffer[4]));
	}
	inline USHORT getMissedCount() const noexcept
	{
		return SWAP16(*(unsigned short*)(&buffer[6]));
	}
	inline void setTimeLessLatency(uint32_t nVal) noexcept
	{
		*(uint32_t *)(&buffer[4]) = SWAP32(nVal);
	}
	inline void setRtpSync(uint32_t nVal) noexcept
	{
		*(uint32_t *)(&buffer[16]) = SWAP32(nVal);
	}
	inline void setRtpData(uint32_t nVal) noexcept
	{
		*(uint32_t *)(&buffer[4]) = SWAP32(nVal);
	}
	inline void setSSRC(uint32_t nVal) noexcept
	{
		*(uint32_t *)(&buffer[8]) = SWAP32(nVal);
	}
	uint8_t* data() noexcept
	{
		return buffer;
	}
	size_t size() const noexcept
	{
		return bufSize;
	}
	void resize(size_t size) noexcept
	{
		assert(size <= sizeof(buffer));

		if (size > bufSize)
		{
			memset(buffer + bufSize, 0, size - bufSize);
		}
		bufSize = size;
	}

private:
	uint8_t buffer[4096];
	size_t bufSize;
};

void PutPacketToPool(std::unique_ptr<RtpPacket>&& p);

class RtpEndpoint;

class IRtpRequestHandler
{
public:
	virtual void OnRequest(RtpEndpoint* endpoint, std::unique_ptr<RtpPacket>&& packet) = 0;
};

class RtpEndpoint 
{
public:
	RtpEndpoint(IRtpRequestHandler* requestHandler, const std::string& peer);
    ~RtpEndpoint();

	bool SendTo(const void* buf, size_t len, USHORT port) noexcept;

	inline uint16_t GetPort() const noexcept
	{
		return m_port;
	}

private:
    void Run() noexcept;

private:
	IRtpRequestHandler*	const       			m_requestHandler;
	const std::string			       			m_peer;
	bool										m_isV4;
    std::unique_ptr<std::thread>    			m_thread;
    std::unique_ptr<sockpp::datagram_socket> 	m_socket;
	uint16_t									m_port;
	std::atomic_bool							m_stop;
};