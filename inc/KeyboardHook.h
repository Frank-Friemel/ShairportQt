#pragma once

#include <memory>
#include "libutils.h"

namespace KeyboardHook
{
	enum Key
	{
		Invalid = 0,
		PlayPause,
		VolumeMute,
		VolumeDown,
		VolumeUp,
		MediaNextTrack,
		MediaPreviousTrack,
		MediaStop,


	};

	class ICallback
	{
	public:
		virtual void OnKeyPressed(Key key) noexcept = 0;
	};

	using Handle = std::unique_ptr<ScopeContext>;

	Handle Setup(ICallback* callback);
}