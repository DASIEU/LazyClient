// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc) ;
#endif

// LazyClient settings page
MACRO_CONFIG_INT(LcUiSettingsTab, lc_ui_settings_tab, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Selected LazyClient settings tab")
