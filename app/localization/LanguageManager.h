#pragma once
#include "LayerCake.h"
#include <languages/ILanguageManager.h>

namespace Localization
{
	class LanguageManager
		: public RefCount<ILanguageManager>
	{
	public:
		LanguageManager();
		HRESULT STDMETHODCALLTYPE QueryInterface(const IID& iid, void** ppv) override;

	public:
		std::string				GetString(int id) const override;
		const std::string		GetCurrentLanguage() const override;
		void					SetCurrentLanguage(const std::string& language) override;
		std::list<std::string>	GetAvailableLanguages() const override;

	private:
		std::string m_currentLanguage;
	};
}