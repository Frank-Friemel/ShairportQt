#include "libutils.h"
#include <stdlib.h>
#include <math.h>
#include <mutex>
#include <stdexcept>
#include <LayerCake.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

using namespace std;
using namespace literals;

ScopeContext::ScopeContext(const function<void()>& scopeCleanup)
	: m_scopeCleanup(scopeCleanup)
{
	if (!m_scopeCleanup)
	{
		throw invalid_argument("scopeCleanup");
	}
}

ScopeContext::~ScopeContext()
{
	try
	{
		m_scopeCleanup();
	}
	catch (...)
	{
	}
}

uint32_t CreateRand(uint32_t nMax /*= numeric_limits<uint32_t>::max()*/)
{
	return static_cast<uint32_t>((static_cast<double>(nMax) * static_cast<double>(rand())) / static_cast<double>(RAND_MAX));
}

vector<unsigned char> DecodeFromHex(const string& str)
{
	vector<unsigned char> result;

	result.reserve((str.length() / 2) + 1);

	bool hi = true;
	unsigned char c = 0;

	for (const auto& x : str)
	{
		const unsigned char n = x >= 'a' ? 10 + (x - 'a') : x >= 'A' ? 10 + (x - 'A') : x - '0';

		if (hi)
		{
			c = (n << 4) & 0xf0;
		}
		else
		{
			c |= n;

			result.emplace_back(c);
		}
		hi = !hi;
	}
	if (!hi)
	{
		result.emplace_back(c >> 4);
	}
	return result;
}

uint64_t HexToInteger(const string& strID) noexcept
{
    uint64_t result = 0;

    const int n = static_cast<int>(strID.length());

    if (n > 0)
    {
        uint64_t f = 1;

        for (int i = n - 1; i >= 0; --i)
        {
            const char c = strID[i];

            if (c >= '0' && c <= '9')
            {
                result += (f * (c - '0'));
            }
            else if (c >= 'a' && c <= 'f')
            {
                result += (f * (c - 'a' + 10));
            }
            else if (c >= 'A' && c <= 'F')
            {
                result += (f * (c - 'A' + 10));
            }
            else
            {
                break;
            }
            f <<= 4;
        }
    }
    return result;
}

void EnableLogToFile(bool enable, const char* logFileName /* = nullptr */)
{
	auto logger = spdlog::default_logger();

	if (!logger)
	{
		throw runtime_error("no default logger defined");
	}

	static mutex logFileMutex;

	const lock_guard<mutex> logGuard(logFileMutex);
	static bool fileLoggerEnabled = false;

	// "log to file" enabled?
	if (enable)
	{
		if (!fileLoggerEnabled)
		{
			// increase the log level
			logger->set_level(spdlog::level::level_enum::debug);

			// create "log to file"
			const spdlog::filename_t logPath = GetHomeDirectoryA() + GetPathDelimiter() + string(logFileName ? logFileName : "LogFile.txt");

			// add file-sink
			auto fileSink = make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath, 100 * 1024, 5, false);
			logger->sinks().emplace_back(move(fileSink));
			fileLoggerEnabled = true;
		}
	}
	else if (fileLoggerEnabled)
	{
		assert(logger->sinks().size() > 1);

		// remove file-sink
		logger->sinks().resize(logger->sinks().size() - 1);

		// decrease the log level
		logger->set_level(spdlog::level::level_enum::info);

		fileLoggerEnabled = false;
	}
}

#ifdef _WIN32

bool GetValueFromRegistry(void* hKey, const char* pValueName, string& strValue, const char* strKeyPath /*= nullptr*/)
{
	HKEY	h = NULL;
	bool	bResult = false;
	string	strKeyName("Software\\ShairportQt");

	if (strKeyPath)
	{
		strKeyName = strKeyPath;
	}
	if (RegOpenKeyExA(static_cast<HKEY>(hKey), strKeyName.c_str(), 0, KEY_READ, &h) == ERROR_SUCCESS)
	{
		DWORD dwSize = 0;

		if (RegQueryValueExA(h, pValueName, NULL, NULL, NULL, &dwSize) == ERROR_SUCCESS)
		{
			dwSize += 2;

			LPBYTE pBuf = new BYTE[dwSize];

			memset(pBuf, 0, dwSize);

			if (RegQueryValueExA(h, pValueName, NULL, NULL, pBuf, &dwSize) == ERROR_SUCCESS)
			{
				strValue = (PCSTR)pBuf;
				bResult = true;
			}
			delete pBuf;
		}
		RegCloseKey(h);
	}
	return bResult;
}

bool PutValueToRegistry(void* hKey, const char* pValueName, const char* pValue, const char* strKeyPath /*= nullptr*/)
{
	HKEY	h;
	DWORD	disp = 0;
	bool	bResult = false;
	string	strKeyName("Software\\ShairportQt");

	if (strKeyPath)
	{
		strKeyName = strKeyPath;
	}
	if (RegCreateKeyExA(static_cast<HKEY>(hKey), strKeyName.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &h, &disp) == ERROR_SUCCESS)
	{
		bResult = RegSetValueExA(h, pValueName, 0, REG_SZ, (CONST BYTE*)pValue, ((unsigned long)strlen(pValue) + 1) * sizeof(char)) == ERROR_SUCCESS;
		RegCloseKey(h);
	}
	return bResult;
}

bool RemoveValueFromRegistry(void* hKey, const char* pValueName, const char* strKeyPath /*= nullptr */)
{
	HKEY	h;
	DWORD	disp = 0;
	bool	bResult = false;
	string	strKeyName("Software\\ShairportQt");

	if (strKeyPath)
	{
		strKeyName = strKeyPath;
	}
	auto error = RegCreateKeyExA(static_cast<HKEY>(hKey), strKeyName.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &h, &disp);

	if (error == ERROR_SUCCESS)
	{
		error = RegDeleteValueA(h, pValueName);
		RegCloseKey(h);
	}
	if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
	{
		bResult = true;
	}
	return bResult;
}

static struct tm* localtime_r(time_t const* const __timer, struct tm* const __tp)
{
	localtime_s(__tp, __timer);
	return __tp;
}

static struct tm* gmtime_r(time_t const* const __timer, struct tm* const __tp)
{
	gmtime_s(__tp, __timer);
	return __tp;
}

#endif

static const double g_ntpMicroSecondsFactor = 1000000.0l * pow(2.0l, (-32.0l));

uint64_t ToNTP(const chrono::system_clock::time_point tp) noexcept
{
	auto tse = tp.time_since_epoch();
	uint32_t seconds = static_cast<uint32_t>(chrono::duration_cast<chrono::seconds>(tse).count());

	tse -= chrono::seconds(seconds);
	const uint32_t fraction = static_cast<double>(chrono::duration_cast<chrono::microseconds>(tse).count()) / g_ntpMicroSecondsFactor;

	// seconds-diff 1900 -> 1970
	seconds += 0x83AA7E80;

	return MAKEUINT64(seconds, fraction);
}

chrono::system_clock::time_point FromNTP(const uint64_t ntp) noexcept
{
	// seconds-diff 1900 -> 1970
	const auto seconds = LODWORD(ntp) - 0x83AA7E80;
	const double lfMicros = static_cast<double>(HIDWORD(ntp)) * g_ntpMicroSecondsFactor;
	const auto micros = static_cast<uint64_t>(lfMicros) +
		(((static_cast<uint64_t>(lfMicros) < 999999) && (static_cast<uint64_t>(lfMicros*100.0l) % 100) == 99) ? 1 : 0);

	const auto ep = chrono::system_clock::time_point();

	return ep + chrono::seconds(seconds) + chrono::microseconds(micros);
}

string ToString(const chrono::system_clock::time_point& tp, const bool utc /*= false*/)
{
	const time_t t = std::chrono::system_clock::to_time_t(tp);

	tm _tm = { 0 };
	char buf[256]{ 0 };

	if (utc)
	{
		gmtime_r(&t, &_tm);
		strftime(buf, 256, "%Y-%m-%dT%H:%M:%S+0000", &_tm);
	}
	else
	{
		localtime_r(&t, &_tm);
		strftime(buf, 256, "%Y-%m-%dT%H:%M:%S%Ez", &_tm);
	}

	string result{ buf };
	const auto i = result.rfind(':');

	if (i != string::npos)
	{
		auto tse = tp.time_since_epoch();
		tse -= chrono::duration_cast<chrono::seconds>(tse);

		result.insert(i+3, "."s + to_string(chrono::duration_cast<chrono::microseconds>(tse).count()));
	}
	return result;
}
