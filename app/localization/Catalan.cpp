#include "StringIDs.h"
#include "exception"
#include "Languages.h"

using namespace std;
using namespace string_literals;

namespace Localization::Catalan
{
	string GetString(int id)
	{
		switch (id)
		{
		case StringID::OK:
			return "OK"s;

		case StringID::CANCEL:
			return "Cancel·lar"s;

		case StringID::CLOSE:
			return "Tancar"s;

		case StringID::BONJOUR_INSTALL:
#ifdef _WIN32
			return R"(<p>Si us plau, instal·la Bonjour per Windows des d'aquest enllaç:</p>
                      <p><a href="https://support.apple.com/kb/DL999">https://support.apple.com/kb/DL999</a></p>)"s;
#else
			return "Si us plau, instal·la el servei avahi-daemon i la llibreria libavahi-compat-libdnssd-dev."s;
#endif
		case StringID::TROUBLE_SHOOT_RAOP_SERVICE:
			return "El servei AirPlay no s'ha pogut iniciar.\nSi us plau, activa el servei Bonjour/Avahi."s;

		case StringID::RECONFIG_RAOP_SERVICE:
			return "L'emissió d'àudio s'aturarà breument per reconfigurar el programa!"s;

		case StringID::FAILED_TO_START_DACP_BROWSER:
			return "El navegador DACP no s'ha pogut iniciar. El Control Multimèdia no es troba disponible."s;

		case StringID::MENU_FILE:
			return "&Fitxer"s;

		case StringID::MENU_QUIT:
			return "&Sortir"s;

		case StringID::MENU_EDIT:
			return "E&ditar"s;

		case StringID::MENU_OPTIONS:
			return "&Opcions..."s;

		case StringID::MENU_HELP:
			return "A&juda"s;

		case StringID::MENU_ABOUT:
			return "&Sobre..."s;

		case StringID::LABEL_AIRPORT_NAME:
			return "Nom Airport"s;

		case StringID::LABEL_AIRPORT_PASSWORD:
			return "Contrasenya"s;

		case StringID::LABEL_AIRPORT_CHANGE:
			return "Cambiar..."s;

		case StringID::LABEL_TITLE_INFO:
			return "Info Pista"s;

		case StringID::TOOLTIP_PREV_TRACK:
			return "Anterior"s;

		case StringID::TOOLTIP_NEXT_TRACK:
			return "Següent"s;

		case StringID::TOOLTIP_VOLUME_UP:
			return "Volum més"s;

		case StringID::TOOLTIP_VOLUME_DOWN:
			return "Volum menys"s;

		case StringID::TOOLTIP_PLAY_PAUSE:
			return "Play/Pause"s;

		case StringID::STATUS_READY:
			return "Tot llest"s;

		case StringID::STATUS_CONNECTED:
			return "Conectat a "s;

		case StringID::DIALOG_CHANGE_NAME_PASSWORD:
			return "Canvia el Nom Airport i Contrasenya"s;

		case StringID::DIALOG_ABOUT:
			return "Sobre"s;

		case StringID::ABOUT_INFO:
			return CW2AEX(L"\xA9"s) + " Copyright 2025\nFrank Friemel\n\nShairportQt està basat Shairport de James Laird\n "s;

		case StringID::OPTION_MINIMIZED:
			return "Obrir minimitzat"s;

		case StringID::OPTION_AUTOSTART:
			return "Autoinici"s;

		case StringID::DIALOG_OPTIONS:
			return "Opcions Avançades"s;

		case StringID::LABEL_DATA_BUFFER:
			return "Buffering"s;

		case StringID::LABEL_MIN:
			return "min"s;

		case StringID::LABEL_MAX:
			return "max"s;

		case StringID::LABEL_SOUND_DEVICE:
			return "Dispositiu de So"s;

		case StringID::LABEL_LOG_TO_FILE:
			return "Guardar registres a fitxer"s;

		case StringID::LABEL_DISABLE_MM_CONTROL:
			return "Desactivar Controls Multimèdia"s;

		case StringID::LABEL_DEFAULT_DEVICE:
			return "Dispositiu per defecte del sistema"s;

		case StringID::LABEL_KEEP_STICKY:
			return "mantenir la finestra sempre visible"s;

		case StringID::LABEL_TRAY_ICON:
			return "Icona de safata"s;

		case StringID::MENU_SHOW_APP_WINDOW:
			return "&Obrir"s;

		case StringID::MENU_SHOW_TRACK_INFO_IN_TRAY:
			return "&Mostra \"Reproduïnt\" a la safata"s;

		}
		return English::GetString(id);
	}
}
