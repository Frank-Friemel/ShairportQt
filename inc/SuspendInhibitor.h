#pragma once
#include <LayerCake.h>
#include <map>
#include <memory>

#ifdef __APPLE__
#include <IOKit/pwr_mgt/IOPMLib.h>
#endif

class SuspendInhibitor final
{
public:
	SuspendInhibitor(const char* strApplication, const char* strReason) noexcept;
	~SuspendInhibitor();

private:
	void Inhibit(bool enable, const char* strApplication = nullptr, const char* strReason = nullptr) const noexcept;

#if defined(__APPLE__)
	IOPMAssertionID m_powerAssertion = kIOPMNullAssertionID;
#elif defined(__linux__)
	mutable std::unique_ptr<std::map<int, Variant>> m_cookies;
#endif
};