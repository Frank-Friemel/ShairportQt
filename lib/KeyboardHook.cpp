#include "KeyboardHook.h"
#include "LayerCake.h"
#include <spdlog/spdlog.h>
#include <mutex>
#include <stdexcept>
#include <map>

namespace KeyboardHook
{
#ifdef _WIN32

	class TheKeyboardHook
	{
	private:
		static LRESULT CALLBACK OnDTKeyboardEvent(int nCode, WPARAM wParam, LPARAM lParam);

		void UnHook(bool report = true) noexcept
		{
			if (m_hKeyboardHook)
			{
				if (UnhookWindowsHookEx(m_hKeyboardHook))
				{
					m_hKeyboardHook = NULL;

					if (report)
					{
						spdlog::debug("succeeded to remove Keyboard-Hook");
					}
				}
				else
				{
					if (report)
					{
						spdlog::error("failed to remove Keyboard-Hook: {}", GetLastError());
					}
				}
			}
		}

	public:
		~TheKeyboardHook()
		{
			UnHook(false);
		}

		Handle Setup(ICallback* callback)
		{
			if (!callback)
			{
				throw std::invalid_argument("callback");
			}
			const std::lock_guard<std::shared_mutex> guard(m_mutex);

			const int id = ++m_counter;
			auto result = std::make_unique<ScopeContext>([this, id]() -> void
				{
					const std::lock_guard<std::shared_mutex> guard(m_mutex);
					m_mapCallback.erase(id);

					if (m_mapCallback.empty())
					{
						UnHook();
					}
				});

			m_mapCallback[id] = callback;

			if (!m_hKeyboardHook)
			{
				m_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, OnDTKeyboardEvent, NULL, 0);

				if (!m_hKeyboardHook)
				{
					spdlog::error("Sorry, no Keyboard-Hook for us: {}", GetLastError());
				}
				else
				{
					spdlog::debug("succeeded to setup Keyboard-Hook");
				}
			}
			return result;
		}

	private:
		std::shared_mutex			m_mutex;
		HHOOK						m_hKeyboardHook{ NULL };
		int							m_counter{ 0 };
		std::map<int, ICallback*>	m_mapCallback;
	};

	static TheKeyboardHook g_theKeyboardHook;

	LRESULT CALLBACK TheKeyboardHook::OnDTKeyboardEvent(int nCode, WPARAM wParam, LPARAM lParam)
	{
		// Attention:
		// The hook procedure should process a message in less time than the data entry specified in the LowLevelHooksTimeout value in the following registry key: 
		// HKEY_CURRENT_USER\Control Panel\Desktop
		// The value is in milliseconds. If the hook procedure times out, the system passes the message to the next hook.
		// However, on Windows 7 and later, the hook is *silently removed* without being called. There is no way for the application to know whether the hook is removed.
		//

		const std::shared_lock<std::shared_mutex> guard(g_theKeyboardHook.m_mutex);

		if (!g_theKeyboardHook.m_mapCallback.empty())
		{
			if (nCode == HC_ACTION)
			{
				if (wParam == WM_KEYDOWN)
				{
					PKBDLLHOOKSTRUCT p = (PKBDLLHOOKSTRUCT)lParam;

					if (p)
					{
						Key key = Key::Invalid;

						// spdlog::debug("key pressed: {}", p->vkCode);

						switch (p->vkCode)
						{
						case VK_MEDIA_PLAY_PAUSE:
						case VK_PAUSE:
						{
							key = Key::PlayPause;
						}
						break;

						case VK_VOLUME_MUTE:
						{
							key = Key::VolumeMute;
						}
						break;

						case VK_VOLUME_DOWN:
						{
							key = Key::VolumeDown;
						}
						break;

						case VK_VOLUME_UP:
						{
							key = Key::VolumeUp;
						}
						break;

						case VK_MEDIA_NEXT_TRACK:
						{
							key = Key::MediaNextTrack;
						}
						break;

						case VK_MEDIA_PREV_TRACK:
						{
							key = Key::MediaPreviousTrack;
						}
						break;

						case VK_MEDIA_STOP:
						{
							key = Key::MediaStop;
						}
						break;
						}

						if (Key::Invalid != key)
						{
							for (const auto& cb : g_theKeyboardHook.m_mapCallback)
							{
								cb.second->OnKeyPressed(key);
							}
						}
					}
				}
			}
		}
		return CallNextHookEx(g_theKeyboardHook.m_hKeyboardHook, nCode, wParam, lParam);
	}

	Handle Setup(ICallback* callback)
	{
		return g_theKeyboardHook.Setup(callback);
	}

#else // _WIN32

	Handle Setup(ICallback* callback)
	{
		return std::make_unique<ScopeContext>([]() -> void
			{
			});
	}

#endif // _WIN32
}
