#pragma once

#include <string>
#include <list>
#include "../LayerCake.h"

// {F3370AE6-E819-4AD8-A5B6-5123AE272606}
const IID IID_ILanguageManager = { 0xf3370ae6, 0xe819, 0x4ad8, 0xa5, 0xb6, 0x51, 0x23, 0xae, 0x27, 0x26, 0x6 };

class ILanguageManager
	: public IUnknown
{
public:
	virtual std::string GetString(int id) const = 0;
	virtual const std::string GetCurrentLanguage() const = 0;
	virtual void SetCurrentLanguage(const std::string& language) = 0;
	virtual std::list<std::string> GetAvailableLanguages() const = 0;
};