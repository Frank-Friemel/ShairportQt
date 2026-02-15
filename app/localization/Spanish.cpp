#include "StringIDs.h"
#include "exception"
#include "Languages.h"

using namespace std;
using namespace string_literals;

namespace Localization::Spanish
{
	string GetString(int id)
	{
		switch (id)
		{
		case StringID::OK:
			return "OK"s;

		case StringID::CANCEL:
			return "Cancelar"s;

		case StringID::CLOSE:
			return "Cerrar"s;

		case StringID::BONJOUR_INSTALL:
#ifdef _WIN32
			return R"(<p>Por favor, instala Bonjour para Windows desde este enlace:</p>
                      <p><a href="https://support.apple.com/kb/DL999">https://support.apple.com/kb/DL999</a></p>)"s;
#else
			return "Por favor, instala el servicio avahi-daemon y la librería libavahi-compat-libdnssd-dev."s;
#endif
		case StringID::TROUBLE_SHOOT_RAOP_SERVICE:
			return "El servicio AirPlay no se ha podido iniciar.\nPor favor, activa el servicio Bonjour/Avahi."s;

		case StringID::RECONFIG_RAOP_SERVICE:
			return "La emisión de audio se detendrá en breve para reconfigurar el programa!"s;

		case StringID::FAILED_TO_START_DACP_BROWSER:
			return "El navegador DACP no se ha podido iniciar. El Control Multimedia no está disponible."s;

		case StringID::MENU_FILE:
			return "&Archivo"s;

		case StringID::MENU_QUIT:
			return "&Salir"s;

		case StringID::MENU_EDIT:
			return "E&ditar"s;

		case StringID::MENU_OPTIONS:
			return "&Opciones..."s;

		case StringID::MENU_HELP:
			return "A&yuda"s;

		case StringID::MENU_ABOUT:
			return "&Acerca de..."s;

		case StringID::LABEL_AIRPORT_NAME:
			return "Nombre Airport"s;

		case StringID::LABEL_AIRPORT_PASSWORD:
			return "Contraseña"s;

		case StringID::LABEL_AIRPORT_CHANGE:
			return "Cambiar..."s;

		case StringID::LABEL_TITLE_INFO:
			return "Info Pista"s;

		case StringID::TOOLTIP_PREV_TRACK:
			return "Anterior"s;

		case StringID::TOOLTIP_NEXT_TRACK:
			return "Siguiente"s;

		case StringID::TOOLTIP_VOLUME_UP:
			return "Volumen más"s;

		case StringID::TOOLTIP_VOLUME_DOWN:
			return "Volumen menos"s;

		case StringID::TOOLTIP_PLAY_PAUSE:
			return "Play/Pause"s;

		case StringID::STATUS_READY:
			return "Listo"s;

		case StringID::STATUS_CONNECTED:
			return "Conectado a "s;

		case StringID::DIALOG_CHANGE_NAME_PASSWORD:
			return "Cambiar Nombre Airport y Contraseña"s;

		case StringID::DIALOG_ABOUT:
			return "Acerca de"s;

		case StringID::ABOUT_INFO:
			return CW2AEX(L"\xA9"s) + " Copyright 2025\nFrank Friemel\n\nShairportQt está basado en Shairport de James Laird\n "s;

		case StringID::OPTION_MINIMIZED:
			return "Iniciar minimizado"s;

		case StringID::OPTION_AUTOSTART:
			return "Autoinicio"s;

		case StringID::DIALOG_OPTIONS:
			return "Opciones Avanzadas"s;

		case StringID::LABEL_DATA_BUFFER:
			return "Buffering"s;

		case StringID::LABEL_MIN:
			return "min"s;

		case StringID::LABEL_MAX:
			return "max"s;

		case StringID::LABEL_SOUND_DEVICE:
			return "Dispositivo de Sonido"s;

		case StringID::LABEL_LOG_TO_FILE:
			return "Guardar registros en archivo"s;

		case StringID::LABEL_DISABLE_MM_CONTROL:
			return "Desactivar Controles Multimedia"s;

		case StringID::LABEL_DEFAULT_DEVICE:
			return "Dispositivo por defecto del sistema"s;

		case StringID::LABEL_KEEP_STICKY:
			return "mantener la ventana siempre visible"s;

		case StringID::LABEL_TRAY_ICON:
			return "Icono de bandeja"s;

		case StringID::MENU_SHOW_APP_WINDOW:
			return "M&ostrar"s;

		case StringID::MENU_SHOW_TRACK_INFO_IN_TRAY:
			return "&Mostrar \"Reproduciendo\" en la bandeja"s;

		}
		return English::GetString(id);
	}
}
