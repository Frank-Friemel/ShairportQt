#include "LanguageManager.h"
#include "Languages.h"
#include "StringIDs.h"
#include <QLocale>

using namespace std;
using namespace string_literals;

namespace Localization
{
    LanguageManager::LanguageManager()
    {
        m_currentLanguage = "en-en"s;

        const QStringList uiLanguages = QLocale::system().uiLanguages();

        if (!uiLanguages.empty())
        {
            SetCurrentLanguage(uiLanguages.front().toStdString());
        }
    }

    HRESULT STDMETHODCALLTYPE LanguageManager::QueryInterface(const IID& iid, void** ppv)
    {
        assert(ppv);
        *ppv = nullptr;

        if (::InlineIsEqualGUID(iid, IID_ILanguageManager))
        {
            *ppv = (IUnknown*)this;
        }
        if (*ppv)
        {
            ((IUnknown*)*ppv)->AddRef();
            return S_OK;
        }
        return RefCount<ILanguageManager>::QueryInterface(iid, ppv);
    }

    string LanguageManager::GetString(int id) const
    {
        if (m_currentLanguage == "de-de"s)
        {
            return German::GetString(id);
        }
        else if (m_currentLanguage == "ja-jp"s)
        {
            return Japanese::GetString(id);
        }
        return English::GetString(id);
    }

    const string LanguageManager::GetCurrentLanguage() const
    {
        return m_currentLanguage;
    }

    void LanguageManager::SetCurrentLanguage(const string& language)
    {
        m_currentLanguage = language;
        transform(m_currentLanguage.begin(), m_currentLanguage.end(), m_currentLanguage.begin(), [](char c) -> char
            {
                if (c == '_')
                {
                    return '-';
                }
                return std::tolower(c);
            });
    }

    list<string> LanguageManager::GetAvailableLanguages() const
    {
        list<string> result;

        result.push_back("en-EN"s);
        result.push_back("ja-JP"s);
        result.push_back("de-DE"s);

        return result;
    }
}