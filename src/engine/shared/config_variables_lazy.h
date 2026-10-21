// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc) ;
#endif

// LazyClient settings page
MACRO_CONFIG_INT(LcUiSettingsTab, lc_ui_settings_tab, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Selected LazyClient settings tab")

// Input modes
MACRO_CONFIG_INT(LcInputMode, lc_input_mode, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Input prediction mode (0 = off, 1 = fast, 2 = best, 3 = saiko, 4 = cloud)")
MACRO_CONFIG_INT(LcInputOthers, lc_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply the selected input mode to other tees")
MACRO_CONFIG_INT(LcFastInputAmount, lc_fast_input_amount, 20, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fast input amount in milliseconds")
MACRO_CONFIG_INT(LcBestInputAmount, lc_best_input_amount, 40, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Best input amount in 0.01 ticks")
MACRO_CONFIG_INT(LcBestInputSmoothing, lc_best_input_smoothing, 0, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Best input smoothing amount (0-100%)")
MACRO_CONFIG_INT(LcBestInputLatencyComp, lc_best_input_latency_comp, 0, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Best input latency compensation (0-50%)")
MACRO_CONFIG_INT(LcSaikoInputAmount, lc_saiko_input_amount, 40, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Saiko input amount in 0.01 ticks")
MACRO_CONFIG_INT(LcCloudInputAmount, lc_cloud_input_amount, 40, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cloud input amount in 0.01 ticks")

// Gores mode
MACRO_CONFIG_INT(LcGoresMode, lc_gores_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Gores mode: hold the gun, fire switches to the hammer")
MACRO_CONFIG_INT(LcGoresIgnoreHeavy, lc_gores_ignore_heavy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ignore shotgun, grenade, laser and ninja (always fire gun and hammer)")
MACRO_CONFIG_INT(LcGoresSwitchDelay, lc_gores_switch_delay, 40, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Delay in milliseconds before switching back from hammer to gun")
MACRO_CONFIG_STR(LcGoresGunKey, lc_gores_gun_key, 32, "w", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Key that fires the gun in gores mode without using the hammer")
MACRO_CONFIG_STR(LcGoresGunKeyPrev, lc_gores_gun_key_prev, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Previous bind for the gores gun key (internal)")

// Snap tap
MACRO_CONFIG_INT(LcSnapTap, lc_snap_tap, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Snap tap: prioritize the last pressed movement direction")
MACRO_CONFIG_INT(LcSnapTapDelay, lc_snap_tap_delay, 0, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Snap tap direction switch delay in milliseconds")

// Antiping
MACRO_CONFIG_INT(LcAntipingMode, lc_antiping_mode, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Antiping mode (0 = off, 1 = on, 2 = auto based on ping)")
MACRO_CONFIG_INT(LcAntipingAutoPing, lc_antiping_auto_ping, 60, 1, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ping threshold in milliseconds at which antiping turns on in auto mode")
MACRO_CONFIG_INT(LcAutoMargin, lc_auto_margin, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically adjust the antiping prediction margin based on ping and jitter")

// Aspect ratio
MACRO_CONFIG_INT(LcAspectWidth, lc_aspect_width, 0, 0, 10000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Aspect ratio width (0 = off, e.g. 16)")
MACRO_CONFIG_INT(LcAspectHeight, lc_aspect_height, 0, 0, 10000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Aspect ratio height (0 = off, e.g. 9)")
