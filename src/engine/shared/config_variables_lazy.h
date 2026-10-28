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

// Better zooming
MACRO_CONFIG_INT(LcZoomStep, lc_zoom_step, 3, 1, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Zoom levels changed per press of lc_zoom_in/lc_zoom_out")
MACRO_CONFIG_STR(LcZoomInKey, lc_zoom_in_key, 32, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Key bound to lc_zoom_in (apply in settings)")
MACRO_CONFIG_STR(LcZoomInKeyPrev, lc_zoom_in_key_prev, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Previous bind for lc_zoom_in (internal)")
MACRO_CONFIG_STR(LcZoomOutKey, lc_zoom_out_key, 32, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Key bound to lc_zoom_out (apply in settings)")
MACRO_CONFIG_STR(LcZoomOutKeyPrev, lc_zoom_out_key_prev, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Previous bind for lc_zoom_out (internal)")

// Antiping
MACRO_CONFIG_INT(LcAntipingMode, lc_antiping_mode, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Antiping mode (0 = off, 1 = on, 2 = auto based on ping)")
MACRO_CONFIG_INT(LcAntipingAutoPing, lc_antiping_auto_ping, 60, 1, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ping threshold in milliseconds at which antiping turns on in auto mode")
MACRO_CONFIG_INT(LcAutoMargin, lc_auto_margin, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically adjust the antiping prediction margin based on ping and jitter")

// Aspect ratio
MACRO_CONFIG_INT(LcAspectWidth, lc_aspect_width, 0, 0, 10000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Aspect ratio width (0 = off, e.g. 16)")
MACRO_CONFIG_INT(LcAspectHeight, lc_aspect_height, 0, 0, 10000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Aspect ratio height (0 = off, e.g. 9)")

// Visuals
MACRO_CONFIG_INT(LcClientLogos, lc_client_logos, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show client logos in nameplates and the scoreboard")
MACRO_CONFIG_INT(LcDevIcon, lc_dev_icon, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the developer star LazyClient icon for your own tee (hidden developer option)")
MACRO_CONFIG_INT(LcBestClientPublicList, lc_bestclient_public_list, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the BestClient public player list to show BestClient logos (network request)")
MACRO_CONFIG_INT(LcFriendHeartScoreboard, lc_friend_heart_scoreboard, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show a heart before friends in the scoreboard")
MACRO_CONFIG_INT(LcBetterVotes, lc_better_votes, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show vote percentages, the pass threshold and remaining yes votes")
MACRO_CONFIG_INT(LcVotePassPercentage, lc_vote_pass_percentage, 50, 1, 99, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vote yes percentage required to pass (match the server setting)")
MACRO_CONFIG_INT(LcZeroGravPredict, lc_zerograv_predict, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Predict freeze tiles after holding a zero-gravity wall position")
MACRO_CONFIG_INT(LcZeroGravMode, lc_zerograv_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Zero-gravity predictor mode: 0 = always on, 1 = key toggles it")
MACRO_CONFIG_INT(LcZeroGravPredictTime, lc_zerograv_predict_time, 5, 1, 10, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Zero-gravity freeze warning time in seconds")
MACRO_CONFIG_INT(LcZeroGravHoldTiles, lc_zerograv_hold_tiles, 4, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Zero-gravity hold area radius in tiles")
MACRO_CONFIG_INT(LcZeroGravSpeedrunGuard, lc_zerograv_speedrun_guard, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Suppress the freeze warning while moving fast (anti-speedrun guard)")
MACRO_CONFIG_INT(LcZeroGravSpeedrunSpeed, lc_zerograv_speedrun_speed, 25, 5, 60, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Anti-speedrun guard horizontal speed threshold in tiles per second")
MACRO_CONFIG_STR(LcZeroGravForcedKey, lc_zerograv_forced_key, 32, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Key that toggles the zero-gravity freeze predictor in forced mode")
MACRO_CONFIG_STR(LcZeroGravForcedKeyPrev, lc_zerograv_forced_key_prev, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Previous bind for the forced zero-gravity key (internal)")
MACRO_CONFIG_INT(LcZeroGravOffsetX, lc_zerograv_offset_x, 0, -300, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Zero-gravity warning horizontal offset in pixels")
MACRO_CONFIG_INT(LcZeroGravOffsetY, lc_zerograv_offset_y, 0, -200, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Zero-gravity warning vertical offset in pixels")
MACRO_CONFIG_INT(LcZeroGravScaleX, lc_zerograv_scale_x, 100, 50, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Zero-gravity warning horizontal scale in percent")
MACRO_CONFIG_INT(LcZeroGravScaleY, lc_zerograv_scale_y, 100, 50, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Zero-gravity warning vertical scale in percent")
MACRO_CONFIG_STR(LcZeroGravText, lc_zerograv_text, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom zero-gravity warning prefix (empty = \"Freeze in\")")
MACRO_CONFIG_COL(LcZeroGravTextColor, lc_zerograv_text_color, 0x0EFFF0, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Zero-gravity warning text color")
MACRO_CONFIG_COL(LcZeroGravBgColor, lc_zerograv_bg_color, 0xDB01E54A, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Zero-gravity warning background color")

// Auto team lock
MACRO_CONFIG_INT(LcAutoTeamLock, lc_auto_team_lock, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically lock your team a while after you join it")
MACRO_CONFIG_INT(LcAutoTeamLockDelay, lc_auto_team_lock_delay, 10, 1, 60, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Delay in seconds before the team is locked after joining")

// Auto kill while frozen
MACRO_CONFIG_INT(LcAutoKillFrozen, lc_auto_kill_frozen, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Kill yourself after being frozen and alone in your team without moving")
MACRO_CONFIG_INT(LcAutoKillFrozenTime, lc_auto_kill_frozen_time, 10, 1, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Seconds without movement in freeze before the auto kill")

// Team invites via whispers
MACRO_CONFIG_INT(LcInviteManager, lc_invite_manager, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Handle !invite whispers using the approve/ask/deny list in lazy_invites.txt")

// Telemetry
MACRO_CONFIG_INT(LcTelemetry, lc_telemetry, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show the LazyClient telemetry overlay")
MACRO_CONFIG_INT(LcTelemetryFps, lc_telemetry_fps, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry: current FPS")
MACRO_CONFIG_INT(LcTelemetryFpsAvg, lc_telemetry_fps_avg, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry: average FPS")
MACRO_CONFIG_INT(LcTelemetryFps1Pct, lc_telemetry_fps_1pct, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry: 1% low FPS")
MACRO_CONFIG_INT(LcTelemetryFps01Pct, lc_telemetry_fps_01pct, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry: 0.1% low FPS")
MACRO_CONFIG_INT(LcTelemetryFrametime, lc_telemetry_frametime, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry: frame time in milliseconds")
MACRO_CONFIG_INT(LcTelemetryPing, lc_telemetry_ping, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry: ping in milliseconds")
MACRO_CONFIG_INT(LcTelemetryJitter, lc_telemetry_jitter, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry: ping jitter in milliseconds")
MACRO_CONFIG_INT(LcTelemetryLoss, lc_telemetry_loss, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry: estimated packet loss percentage")
MACRO_CONFIG_INT(LcTelemetryPrediction, lc_telemetry_prediction, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry: prediction time in milliseconds")
MACRO_CONFIG_INT(LcTelemetrySnaps, lc_telemetry_snaps, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry: snapshots received per second")
MACRO_CONFIG_INT(LcTelemetryOffsetX, lc_telemetry_offset_x, 0, -300, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry overlay horizontal offset in pixels")
MACRO_CONFIG_INT(LcTelemetryOffsetY, lc_telemetry_offset_y, 0, -200, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry overlay vertical offset in pixels")
MACRO_CONFIG_INT(LcTelemetryScaleX, lc_telemetry_scale_x, 100, 50, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry overlay horizontal scale in percent")
MACRO_CONFIG_INT(LcTelemetryScaleY, lc_telemetry_scale_y, 100, 50, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry overlay vertical scale in percent")
MACRO_CONFIG_COL(LcTelemetryTextColor, lc_telemetry_text_color, 0x95FFF2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Telemetry overlay text color")
MACRO_CONFIG_COL(LcTelemetryBgColor, lc_telemetry_bg_color, 0xB89C550B, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Telemetry overlay background color")
