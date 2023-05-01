#include "StringIDs.h"
#include "exception"
#include "Languages.h"

using namespace std;
using namespace string_literals;

namespace Localization::German
{
	string GetString(int id)
	{
		switch (id)
		{
		case StringID::OK:
			return "OK"s;

		case StringID::CLOSE:
			return "Schlie"s + CW2AEX(L"\xdf"s) + "en"s;

		case StringID::CANCEL:
			return "Abbrechen"s;

		case StringID::BONJOUR_INSTALL:
#ifdef _WIN32
			return R"(<p>Installiere bitte Bonjour f&uuml;r Windows von hier:</p>
                      <p><a href="https://support.apple.com/kb/DL999">https://support.apple.com/kb/DL999</a></p>)"s;
#else
			return "Installiere bitte avahi-daemon und libavahi-compat-libdnssd-dev"s;
#endif

		case StringID::TROUBLE_SHOOT_RAOP_SERVICE:
			return "Der AirPlay Service konnte nicht gestartet werden.\nAktiviere bitte deinen Bonjour/Avahi-Service."s;

		case StringID::RECONFIG_RAOP_SERVICE:
			return "Der aktuelle Stream mu"s + CW2AEX(L"\xdf"s) + " f"s + CW2AEX(L"\xfc"s) + "r eine Rekonfiguration unterbrochen werden. Entschuldigung!"s;

		case StringID::FAILED_TO_START_DACP_BROWSER:
			return "Der DACP Browser konnte nicht gestartet werden. Media Control ist nicht verf"s + CW2AEX(L"\xfc"s) + "gbar."s;

		case StringID::MENU_FILE:
			return "&Datei"s;

		case StringID::MENU_QUIT:
			return "&Beenden"s;

		case StringID::MENU_EDIT:
			return "B&earbeiten"s;

		case StringID::MENU_OPTIONS:
			return "&Optionen..."s;

		case StringID::MENU_HELP:
			return "&Hilfe"s;

		case StringID::MENU_ABOUT:
			return CW2AEX(L"\xdc"s) + "be&r..."s;
		
		case StringID::LABEL_AIRPORT_NAME:
			return "Airport Name"s;		
		
		case StringID::LABEL_AIRPORT_PASSWORD:
			return "Passwort"s;

		case StringID::LABEL_AIRPORT_CHANGE:
			return CW2AEX(L"\xc4"s) + "ndern..."s;

		case StringID::LABEL_TITLE_INFO:
			return "Titel-Info"s;

		case StringID::TOOLTIP_PREV_TRACK:
			return "Zur"s + CW2AEX(L"\xfc"s) + "ck"s;

		case StringID::TOOLTIP_NEXT_TRACK:
			return "Vorw"s + CW2AEX(L"\xe4"s) + "rts"s;

		case StringID::TOOLTIP_PLAY_PAUSE:
			return "Abspielen/Pause"s;

		case StringID::STATUS_READY:
			return "Bereit"s;

		case StringID::STATUS_CONNECTED:
			return "Verbunden mit "s;

		case StringID::DIALOG_CHANGE_NAME_PASSWORD:
			return "Airport Name und Passwort "s + CW2AEX(L"\xe4"s) + "ndern"s;

		case StringID::DIALOG_ABOUT:
			return CW2AEX(L"\xdc"s) + "ber"s;

		case StringID::ABOUT_INFO:
			return CW2AEX(L"\xA9"s) + " Copyright 2024\nFrank Friemel\n\nShairportQt basiert auf Shairport von James Laird\n "s;

		case StringID::OPTION_MINIMIZED:
			return "Minimiert Starten"s;
		
		case StringID::OPTION_AUTOSTART:
			return "Autostart"s;

		case StringID::DIALOG_OPTIONS:
			return "Erweiterte Optionen"s;

		case StringID::LABEL_DATA_BUFFER:
			return "Daten Puffer"s;		
		
		case StringID::LABEL_MIN:
			return "min"s;

		case StringID::LABEL_MAX:
			return "max"s;

		case StringID::LABEL_SOUND_DEVICE:
			return "Ausgabe Ger"s + CW2AEX(L"\xe4"s) + "t"s;

		case StringID::LABEL_LOG_TO_FILE:
			return "Log Datei"s;		

		case StringID::LABEL_DISABLE_MM_CONTROL:
			return "Multimedia Kontrolle deaktivieren"s;

		case StringID::LABEL_DEFAULT_DEVICE:
			return "wie im System eingestellt"s;		

		case StringID::LABEL_KEEP_STICKY:
			return "Fenster im Vordergrund"s;		

		case StringID::LABEL_TRAY_ICON:
			return "Tray Icon"s;		

		case StringID::MENU_SHOW_APP_WINDOW:
			return "&Anzeigen"s;			

		case StringID::MENU_SHOW_TRACK_INFO_IN_TRAY:
			return "\"Aktuelle Titel - Info\" in der &Tray anzeigen"s;

		}
		return English::GetString(id);
	}
}