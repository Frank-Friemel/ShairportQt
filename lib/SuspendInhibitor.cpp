#include "SuspendInhibitor.h"
#include <spdlog/spdlog.h>
#include <assert.h>

#ifdef __APPLE__

#	include <IOKit/pwr_mgt/IOPMLib.h>

#elif defined(__linux__)

#	include <QtDBus/QtDBus>
#	include <QDBusConnection>
#	include <QDBusReply>
#	include <QString>
#	include <unistd.h>
#	include <mutex>
#	include <string>

#endif

using namespace std;
using namespace string_literals;

namespace 
{
#ifdef __linux__

	class LinuxSuspendInhibitor
	{
	public:
		LinuxSuspendInhibitor() 
			: m_bus(QDBusConnection::sessionBus())
		{
			if (m_bus.isConnected())
			{
				auto interface = m_bus.interface();

				if (interface)
				{
					auto reply = interface->registeredServiceNames();

					if (reply.isValid())
					{
						m_availableServices = reply.value();
#if 0
						for (const auto& i : m_availableServices)
						{
							spdlog::debug("{}", i.toStdString());
						}
#endif
					}
				}
			}
			else
			{
				spdlog::error("failed to connect to dbus");
			}
		}

		template <typename T>
		void LogReply(const QDBusReply<T>& reply, const char* name) const;
		void Inhibit(bool enable, map<int, Variant>& cookies, const char* strApplication = nullptr,
			const char* strReason = nullptr) const;

	private:
		const QDBusConnection m_bus;
		QStringList m_availableServices;
	};

	template<>
	void LinuxSuspendInhibitor::LogReply(const QDBusReply<void>& reply, const char* name) const
	{
		if (reply.isValid())
		{
			spdlog::debug("QDBus {} succesful", name);
		}
		else
		{
			QDBusError error = reply.error();
			spdlog::error("QDBus Error: {} {}", error.name().toStdString(), error.message().toStdString());
		}
	}

	template <typename T>
	void LinuxSuspendInhibitor::LogReply(const QDBusReply<T>& reply, const char* name) const
	{
		if (reply.isValid())
		{
			spdlog::debug("QDBus {} succesful", name);
		}
		else
		{
			QDBusError error = reply.error();
			spdlog::error("QDBus Error {}: {}", error.name().toStdString(), error.message().toStdString());
		}
	}

	void LinuxSuspendInhibitor::Inhibit(bool enable, map<int, Variant>& cookies, const char* strApplication /*= nullptr*/,
			const char* strReason /*= nullptr*/) const
	{
		if (m_bus.isConnected())
		{
			const map<int, pair<QString, QString>> mapServices {
				{ 0, { "org.gnome.SessionManager", "/org/gnome/SessionManager" } },
				{ 1, { "org.freedesktop.login1", "/org/freedesktop/login1" } },
				{ 2, { "org.freedesktop.PowerManagement.Inhibit", "/org/freedesktop/PowerManagement/Inhibit" } }
			};

			for (const auto& entry : mapServices)
			{
				const auto& service = entry.second;

				if (!m_availableServices.contains(service.first))
				{
					continue;
				}
				QDBusInterface sessionManagerInterface(service.first, service.second, service.first, m_bus);

				if (!sessionManagerInterface.isValid())
				{
					spdlog::error("failed to connect to {}", service.first.toStdString());
					continue;
				}
				try
				{
					switch(entry.first)
					{
						case 0:
						{
							uint32_t xid = 0;
							uint32_t flags = 0x4; // Inhibit suspending the session or computer

							if (enable)
							{
								const QDBusReply<unsigned int> reply = sessionManagerInterface.call("Inhibit", strApplication, xid, strReason, flags);

								if (reply.isValid())
								{
									const Variant varValue(reply.value(), VT_UI4);
									cookies[entry.first] = varValue;
									spdlog::debug("got cookie {} for {}", varValue.ulVal, service.first.toStdString());
								}
								else
								{
									LogReply(reply, "inhibit");
								}
							}
							else
							{
								const auto i = cookies.find(entry.first);

								if (i != cookies.end() && i->second.vt != VT_EMPTY)
								{
									const QDBusReply<void> reply = sessionManagerInterface.call("Uninhibit", static_cast<unsigned int>(i->second.ulVal));
									LogReply(reply, "uninhibit");
								}
							}
						}
						break;

						case 1:
						{
							if (enable)
							{
								const QDBusReply<int> reply = sessionManagerInterface.call("Inhibit", "sleep", strApplication, strReason, "block");

								if (reply.isValid())
								{
									const Variant varValue(reply.value(), VT_I4);
									cookies[entry.first] = varValue;
									spdlog::debug("got cookie {} for {}", varValue.lVal, service.first.toStdString());
								}
								else
								{
									LogReply(reply, "inhibit");
								}
							}
							else
							{
								const auto i = cookies.find(entry.first);

								if (i != cookies.end() && i->second.vt != VT_EMPTY)
								{							
									const int error = close(static_cast<int>(i->second.lVal));

									if (error == 0)
									{
										spdlog::debug("successfully uninhibited sleep mode");
									}
									else
									{
										spdlog::error("failed to uninhibit sleep mode: {}", error);
									}
								}
							}
						}
						break;

						case 2:
						{
							if (enable)
							{
								const QDBusReply<unsigned int> reply = sessionManagerInterface.call("Inhibit", strApplication, strReason);

								if (reply.isValid())
								{
									const Variant varValue(reply.value(), VT_UI4);
									cookies[entry.first] = varValue;
									spdlog::debug("got cookie {} for {}", varValue.ulVal, service.first.toStdString());
								}
								else
								{
									LogReply(reply, "inhibit");								
								}
							}
							else
							{
								const auto i = cookies.find(entry.first);

								if (i != cookies.end() && i->second.vt != VT_EMPTY)
								{	
									const QDBusReply<void> reply = sessionManagerInterface.call("UnInhibit", static_cast<unsigned int>(i->second.ulVal));
									LogReply(reply, "uninhibit");
								}
							}
						}
						break;					
					}
				}
				catch(...)
				{
					spdlog::error("exception during {} for {}", enable ? "inhibit" : "unhibit", entry.second.first.toStdString());
				}
			}
		}
	}

	static mutex mtxLinuxInhibitor;
	static unique_ptr<LinuxSuspendInhibitor> linuxInhibitor;

#endif // __linux__

}

void SuspendInhibitor::Inhibit(bool enable, const char* strApplication /*= nullptr*/, const char* strReason /*= nullptr*/) const noexcept
{
	// Prevent OS from suspending
#if defined(_WIN32)
	
	(void)strApplication;
	(void)strReason;

	const EXECUTION_STATE prev = SetThreadExecutionState(ES_CONTINUOUS | (enable ? (ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED) : 0));

	if (prev == 0)
	{
		spdlog::error("{} suspend mode failed", enable ? "Inhibit"s : "Uninhibit"s);
	}
	else
	{
		spdlog::debug("{} suspend mode successful", enable ? "Inhibit"s : "Uninhibit"s);
	}

#elif defined(__APPLE__)

	try
	{
		if (enable)
		{
			const string strName = string(strApplication) + " "s + string(strReason);
			CFStringRef name = CFSTR(strName.c_str());

			if (IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleSystemSleep,
				kIOPMAssertionLevelOn, name,
				&m_powerAssertion) != kIOReturnSuccess)
			{
				m_powerAssertion = kIOPMNullAssertionID;
			}
		}
		else
		{
			if (m_powerAssertion != kIOPMNullAssertionID)
			{
				IOPMAssertionRelease(m_powerAssertion);
				m_powerAssertion = kIOPMNullAssertionID;
			}
		}
	}
	catch (...)
	{
	}

#elif defined(__linux__)

	try
	{
		{
			const lock_guard<mutex> guard(mtxLinuxInhibitor);
			if (!linuxInhibitor)
			{
				linuxInhibitor = make_unique<LinuxSuspendInhibitor>();
			}
		}
		if (enable)
		{
			assert(!m_cookies);
			m_cookies = make_unique<std::map<int, Variant>>();
		}
		else
		{
			if (!m_cookies)
			{
				return;
			}
		}
		linuxInhibitor->Inhibit(enable, *m_cookies, strApplication, strReason);
	}
	catch (...)
	{
	}

#endif
}

SuspendInhibitor::SuspendInhibitor(const char* strApplication, const char* strReason) noexcept
{
	assert(strApplication);
	assert(strReason);

	Inhibit(true, strApplication, strReason);
}

SuspendInhibitor::~SuspendInhibitor()
{
	Inhibit(false);
}
