#pragma once

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include <limits>
#include <stdint.h>
#include <string>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#include <utility>
#include <functional>

uint64_t HexToInteger(const std::string& strID) noexcept;

uint32_t CreateRand(uint32_t nMax = std::numeric_limits<uint32_t>::max());

std::vector<unsigned char> DecodeFromHex(const std::string& str);

void EnableLogToFile(bool enable, const char* logFileName = nullptr);

#ifdef _WIN32

bool GetValueFromRegistry(void* hKey, const char* pValueName, std::string& strValue, const char* strKeyPath = nullptr);
bool PutValueToRegistry(void* hKey, const char* pValueName, const char* pValue, const char* strKeyPath = nullptr);
bool RemoveValueFromRegistry(void* hKey, const char* pValueName, const char* strKeyPath = nullptr);

#endif

template<typename T>
std::string EncodeToHex(const T& buffer, bool bForceUppercase = false)
{
	std::stringstream ss;

	for (const auto& i : buffer) 
	{
		if (bForceUppercase)
		{
			ss << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)i;
		}
		else
		{
			ss << std::hex << std::setw(2) << std::setfill('0')	<< (unsigned int)i;
		}
	}
	return ss.str();
}


class ScopeContext
{
public:
	ScopeContext(const std::function<void()>& scopeCleanup);
	~ScopeContext();

	ScopeContext(const ScopeContext&) = delete;
	ScopeContext& operator=(const ScopeContext&) = delete;

private:
	const std::function<void()> m_scopeCleanup;
};
