#include "StringIDs.h"
#include "exception"
#include "Languages.h"

using namespace std;
using namespace string_literals;

namespace Localization::English
{
	string GetString(int id)
	{
		switch (id)
		{
		case StringID::OK:
			return "OK"s;

		case StringID::CANCEL:
			return "Cancel"s;

		case StringID::CLOSE:
			return "Close"s;

		case StringID::BONJOUR_INSTALL:
#ifdef _WIN32
			return R"(<p>Please install Bonjour for Windows from here:</p>
                      <p><a href="https://support.apple.com/kb/DL999">https://support.apple.com/kb/DL999</a></p>)"s;
#else
			return "Please install avahi-daemon and libavahi-compat-libdnssd-dev"s;
#endif
		case StringID::TROUBLE_SHOOT_RAOP_SERVICE:
			return "The AirPlay service could not be started.\nPlease enable your Bonjour/Avahi-Service."s;

		case StringID::RECONFIG_RAOP_SERVICE:
			return "Current stream will be interrupted for reconfiguration, sorry!"s;

		case StringID::FAILED_TO_START_DACP_BROWSER:
			return "The DACP browser could not be startet. Media Control is not available."s;

		case StringID::MENU_FILE:
			return "&File"s;

		case StringID::MENU_QUIT:
			return "&Quit"s;

		case StringID::MENU_EDIT:
			return "E&dit"s;

		case StringID::MENU_OPTIONS:
			return "&Options..."s;

		case StringID::MENU_HELP:
			return "&Help"s;

		case StringID::MENU_ABOUT:
			return "&About..."s;

		case StringID::LABEL_AIRPORT_NAME:
			return "Airport Name"s;

		case StringID::LABEL_AIRPORT_PASSWORD:
			return "Password"s;

		case StringID::LABEL_AIRPORT_CHANGE:
			return "Change..."s;

		case StringID::LABEL_TITLE_INFO:
			return "Track-Info"s;

		case StringID::TOOLTIP_PREV_TRACK:
			return "Backwards"s;

		case StringID::TOOLTIP_NEXT_TRACK:
			return "Forward"s;

		case StringID::TOOLTIP_PLAY_PAUSE:
			return "Play/Pause"s;

		case StringID::STATUS_READY:
			return "Ready"s;

		case StringID::STATUS_CONNECTED:
			return "Connected to "s;

		case StringID::DIALOG_CHANGE_NAME_PASSWORD:
			return "Change Airport Name and Password"s;

		case StringID::DIALOG_ABOUT:
			return "About"s;

		case StringID::ABOUT_INFO:
			return CW2AEX(L"\xA9"s) + " Copyright 2024\nFrank Friemel\n\nShairportQt is based on Shairport by James Laird\n "s;

		case StringID::OPTION_MINIMIZED:
			return "Start minimized"s;

		case StringID::OPTION_AUTOSTART:
			return "Autostart"s;

		case StringID::DIALOG_OPTIONS:
			return "Advanced Options"s;

		case StringID::LABEL_DATA_BUFFER:
			return "Buffering"s;

		case StringID::LABEL_MIN:
			return "min"s;

		case StringID::LABEL_MAX:
			return "max"s;

		case StringID::LABEL_SOUND_DEVICE:
			return "Sound Device"s;

		case StringID::LABEL_LOG_TO_FILE:
			return "Log to file"s;

		case StringID::LABEL_DISABLE_MM_CONTROL:
			return "Disable Multimedia Controls"s;

		case StringID::LABEL_DEFAULT_DEVICE:
			return "System default Device"s;

		case StringID::LABEL_KEEP_STICKY:
			return "keep Window sticky"s;

		case StringID::LABEL_TRAY_ICON:
			return "Tray Icon"s;

		case StringID::MENU_SHOW_APP_WINDOW:
			return "Sh&ow"s;

		case StringID::MENU_SHOW_TRACK_INFO_IN_TRAY:
			return "&Show \"Now Playing\" in Tray"s;

		}
		throw runtime_error("undefined string-id");
	}
}