#pragma once
#include "LayerCake.h"
#include "languages/ILanguageManager.h"
#include <vector>

enum StringID {
	OK = 1,
	CANCEL,
	CLOSE,
	MENU_FILE,
	MENU_QUIT,
	MENU_EDIT,
	MENU_OPTIONS,
	MENU_HELP,
	MENU_ABOUT,
	MENU_SHOW_APP_WINDOW,
	MENU_SHOW_TRACK_INFO_IN_TRAY,

	LABEL_AIRPORT_NAME,
	LABEL_AIRPORT_PASSWORD,
	LABEL_AIRPORT_CHANGE,
	LABEL_TITLE_INFO,
	LABEL_DATA_BUFFER,
	LABEL_MIN,
	LABEL_MAX,
	LABEL_SOUND_DEVICE,
	LABEL_LOG_TO_FILE,
	LABEL_DISABLE_MM_CONTROL,
	LABEL_DEFAULT_DEVICE,
	LABEL_KEEP_STICKY,
	LABEL_TRAY_ICON,

	TOOLTIP_PREV_TRACK,
	TOOLTIP_NEXT_TRACK,
	TOOLTIP_PLAY_PAUSE,

	STATUS_READY,
	STATUS_CONNECTED,

	DIALOG_CHANGE_NAME_PASSWORD,
	DIALOG_ABOUT,
	DIALOG_OPTIONS,
	ABOUT_INFO,

	OPTION_MINIMIZED,
	OPTION_AUTOSTART,

	BONJOUR_INSTALL,
	TROUBLE_SHOOT_RAOP_SERVICE,
	RECONFIG_RAOP_SERVICE,
	FAILED_TO_START_DACP_BROWSER,
};

 enum MsgBoxType {
	NoIcon = 0,
	Information = 1,
	Warning = 2,
	Critical = 3,
	Question = 4
};

int ShowMessageBox(const SharedPtr<IValueCollection>& config, int message, void* parent = nullptr, MsgBoxType type = MsgBoxType::Information,
	std::vector<int> buttons = { StringID::OK });

SharedPtr<ILanguageManager> GetLanguageManager(const SharedPtr<IValueCollection>& config);
