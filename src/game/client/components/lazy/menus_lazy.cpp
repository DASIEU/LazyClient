#include <base/io.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>
#include <game/version.h>

enum
{
	LAZY_TAB_GAME = 0,
	LAZY_TAB_VISUALS,
	LAZY_TAB_CHAT_TRIGGERS,
	LAZY_TAB_MISCELLANEOUS,
	LAZY_TAB_INFO,
	NUM_LAZY_TABS,
};

static constexpr float LazyLineSize = 20.0f;
static constexpr float LazyMargin = 10.0f;
static constexpr float LazyMarginSmall = 5.0f;
static constexpr float LazyHeadlineSize = 18.0f;
static constexpr float LazyHeadlineHeight = 24.0f;

static void LazySectionTitle(CUIRect *pRect, const char *pTitle, CUi *pUi)
{
	CUIRect Label;
	pRect->HSplitTop(LazyHeadlineHeight, &Label, pRect);
	pUi->DoLabel(&Label, pTitle, LazyHeadlineSize, TEXTALIGN_ML);
	pRect->HSplitTop(LazyMarginSmall, nullptr, pRect);
}

static void LazyInfoLabel(CUIRect *pRect, const char *pText, CUi *pUi, ITextRender *pTextRender)
{
	CUIRect Label;
	pRect->HSplitTop(16.0f, &Label, pRect);
	pTextRender->TextColor(1.0f, 1.0f, 1.0f, 0.65f);
	pUi->DoLabel(&Label, pText, 12.0f, TEXTALIGN_ML);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
}

static void LazyBeginScroll(CUIRect *pRect, CScrollRegion *pScrollRegion, CScrollRegionParams *pParams)
{
	pParams->m_ScrollUnit = 60.0f;
	pParams->m_ForceShowScrollbar = true;
	pParams->m_ScrollbarMargin = 5.0f;
	pScrollRegion->Begin(pRect, pParams);
	CUIRect Inner = *pRect;
	Inner.VSplitRight(5.0f, &Inner, nullptr);
	Inner.VSplitLeft(5.0f, nullptr, &Inner);
	*pRect = Inner;
}

void CMenus::RenderSettingsLazy(CUIRect MainView)
{
	CUIRect TabBar, Button;
	MainView.HSplitTop(LazyLineSize, &TabBar, &MainView);

	const float TabWidth = TabBar.w / (float)NUM_LAZY_TABS;
	static CButtonContainer s_aPageTabs[NUM_LAZY_TABS] = {};
	static const char *s_apTabNames[NUM_LAZY_TABS] = {
		"Game",
		"Visuals",
		"Chat triggers",
		"Miscellaneous",
		"Info",
	};

	if(g_Config.m_LcUiSettingsTab < 0 || g_Config.m_LcUiSettingsTab >= NUM_LAZY_TABS)
		g_Config.m_LcUiSettingsTab = LAZY_TAB_GAME;

	for(int Tab = 0; Tab < NUM_LAZY_TABS; ++Tab)
	{
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : Tab == NUM_LAZY_TABS - 1 ? IGraphics::CORNER_R :
												IGraphics::CORNER_NONE;
		if(DoButton_MenuTab(&s_aPageTabs[Tab], Localize(s_apTabNames[Tab]), g_Config.m_LcUiSettingsTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			g_Config.m_LcUiSettingsTab = Tab;
	}

	MainView.HSplitTop(LazyMargin, nullptr, &MainView);

	switch(g_Config.m_LcUiSettingsTab)
	{
	case LAZY_TAB_GAME:
		RenderSettingsLazyGame(MainView);
		break;
	case LAZY_TAB_VISUALS:
		RenderSettingsLazyVisuals(MainView);
		break;
	case LAZY_TAB_CHAT_TRIGGERS:
		RenderSettingsLazyChatTriggers(MainView);
		break;
	case LAZY_TAB_MISCELLANEOUS:
		RenderSettingsLazyMiscellaneous(MainView);
		break;
	case LAZY_TAB_INFO:
		RenderSettingsLazyInfo(MainView);
		break;
	default:
		break;
	}
}

void CMenus::RenderSettingsLazyGame(CUIRect MainView)
{
	CUIRect Left, Right, Button;
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	LazyBeginScroll(&MainView, &s_ScrollRegion, &ScrollParams);

	MainView.VSplitMid(&Left, &Right, 30.0f);
	Left.VSplitLeft(5.0f, nullptr, &Left);
	Right.VSplitRight(5.0f, &Right, nullptr);

	// Left column: Gores preset and TClient prediction.
	LazySectionTitle(&Left, Localize("Gores Preset"), Ui());
	LazyInfoLabel(&Left, Localize("One click sets the prediction and recording options most gores players use."), Ui(), TextRender());
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	Left.HSplitTop(LazyLineSize * 1.5f, &Button, &Left);
	static CButtonContainer s_ApplyGoresPreset;
	if(DoButton_Menu(&s_ApplyGoresPreset, Localize("Apply Gores Preset"), 0, &Button))
	{
		g_Config.m_TcAntiPingImproved = 1;
		g_Config.m_TcAntiPingNegativeBuffer = 1;
		g_Config.m_TcAntiPingStableDirection = 1;
		g_Config.m_TcAntiPingUncertaintyScale = 150;
		g_Config.m_TcFastInput = 1;
		g_Config.m_TcFastInputAmount = 20;
		g_Config.m_TcFastInputRepredictHook = 1;
		g_Config.m_ClAntiPing = 1;
		g_Config.m_ClAntiPingPlayers = 1;
		g_Config.m_ClAntiPingGrenade = 1;
		g_Config.m_ClAntiPingWeapons = 1;
		g_Config.m_ClAntiPingGunfire = 1;
		g_Config.m_ClPredictionMargin = 10;
		g_Config.m_ClPredictFreeze = 1;
		g_Config.m_ClAutoRaceRecord = 1;
		g_Config.m_ClRaceGhost = 1;
		g_Config.m_ClRaceShowGhost = 1;
	}
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	Left.HSplitTop(LazyLineSize, &Button, &Left);
	static CButtonContainer s_ResetGoresPreset;
	if(DoButton_Menu(&s_ResetGoresPreset, Localize("Reset Game Options"), 0, &Button))
	{
		ConfigManager()->Reset("tc_antiping_improved");
		ConfigManager()->Reset("tc_antiping_negative_buffer");
		ConfigManager()->Reset("tc_antiping_stable_direction");
		ConfigManager()->Reset("tc_antiping_uncertainty_scale");
		ConfigManager()->Reset("tc_fast_input");
		ConfigManager()->Reset("tc_fast_input_amount");
		ConfigManager()->Reset("tc_fast_input_repredict_hook");
		ConfigManager()->Reset("cl_antiping");
		ConfigManager()->Reset("cl_antiping_players");
		ConfigManager()->Reset("cl_antiping_grenade");
		ConfigManager()->Reset("cl_antiping_weapons");
		ConfigManager()->Reset("cl_antiping_gunfire");
		ConfigManager()->Reset("cl_prediction_margin");
		ConfigManager()->Reset("cl_predict_freeze");
		ConfigManager()->Reset("cl_auto_race_record");
		ConfigManager()->Reset("cl_race_ghost");
		ConfigManager()->Reset("cl_race_show_ghost");
	}

	Left.HSplitTop(LazyMargin * 1.5f, nullptr, &Left);
	LazySectionTitle(&Left, Localize("Prediction"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingImproved, Localize("Improved antiping smoothing"), &g_Config.m_TcAntiPingImproved, &Left, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingNegativeBuffer, Localize("Negative certainty buffer (gores)"), &g_Config.m_TcAntiPingNegativeBuffer, &Left, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingStableDirection, Localize("Predict optimistically along stable axis"), &g_Config.m_TcAntiPingStableDirection, &Left, LazyLineSize);
	Left.HSplitTop(LazyLineSize, &Button, &Left);
	Ui()->DoScrollbarOption(&g_Config.m_TcAntiPingUncertaintyScale, &g_Config.m_TcAntiPingUncertaintyScale, &Button, Localize("Uncertainty duration"), 50, 400, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");

	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	LazySectionTitle(&Left, Localize("Input"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInput, Localize("Fast input"), &g_Config.m_TcFastInput, &Left, LazyLineSize);
	Left.HSplitTop(LazyLineSize, &Button, &Left);
	Ui()->DoScrollbarOption(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Button, Localize("Fast input window"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "ms");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputRepredictHook, Localize("Repredict hook on direction change"), &g_Config.m_TcFastInputRepredictHook, &Left, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, Localize("Sub-tick aiming"), &g_Config.m_ClSubTickAiming, &Left, LazyLineSize);

	// Right column: DDNet prediction and recording.
	LazySectionTitle(&Right, Localize("DDNet Prediction"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClAntiPing, Localize("Enable antiping"), &g_Config.m_ClAntiPing, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClAntiPingGrenade, Localize("Predict grenades"), &g_Config.m_ClAntiPingGrenade, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClAntiPingWeapons, Localize("Predict weapons"), &g_Config.m_ClAntiPingWeapons, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClAntiPingGunfire, Localize("Predict gunfire"), &g_Config.m_ClAntiPingGunfire, &Right, LazyLineSize);
	Right.HSplitTop(LazyLineSize, &Button, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_ClAntiPingPlayers, &g_Config.m_ClAntiPingPlayers, &Button, Localize("Predict other players"), 0, 3, &CUi::ms_LinearScrollbarScale, 0, "");
	Right.HSplitTop(LazyLineSize, &Button, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, Localize("Prediction margin"), 1, 300, &CUi::ms_LinearScrollbarScale, 0, "ms");
	Right.HSplitTop(LazyLineSize, &Button, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_ClPredictFreeze, &g_Config.m_ClPredictFreeze, &Button, Localize("Predict freeze tiles"), 0, 2, &CUi::ms_LinearScrollbarScale, 0, "");

	Right.HSplitTop(LazyMargin * 1.5f, nullptr, &Right);
	LazySectionTitle(&Right, Localize("Recording"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClAutoRaceRecord, Localize("Auto-record race demos"), &g_Config.m_ClAutoRaceRecord, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClRaceGhost, Localize("Enable race ghost"), &g_Config.m_ClRaceGhost, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClRaceShowGhost, Localize("Show race ghost"), &g_Config.m_ClRaceShowGhost, &Right, LazyLineSize);
	Right.HSplitTop(LazyLineSize, &Button, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_ClRaceGhostAlpha, &g_Config.m_ClRaceGhostAlpha, &Button, Localize("Ghost opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClReplays, Localize("Enable replays"), &g_Config.m_ClReplays, &Right, LazyLineSize);
	if(g_Config.m_ClReplays)
	{
		Right.HSplitTop(LazyLineSize, &Button, &Right);
		Ui()->DoScrollbarOption(&g_Config.m_ClReplayLength, &g_Config.m_ClReplayLength, &Button, Localize("Replay length"), 10, 3600, &CUi::ms_LogarithmicScrollbarScale, 0, "s");
	}

	s_ScrollRegion.End();
}

void CMenus::RenderSettingsLazyVisuals(CUIRect MainView)
{
	CUIRect Left, Right, Button;
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	LazyBeginScroll(&MainView, &s_ScrollRegion, &ScrollParams);

	MainView.VSplitMid(&Left, &Right, 30.0f);
	Left.VSplitLeft(5.0f, nullptr, &Left);
	Right.VSplitRight(5.0f, &Right, nullptr);

	LazySectionTitle(&Left, Localize("Gores Visuals"), Ui());
	LazyInfoLabel(&Left, Localize("Highlights freeze, kill and teleporter tiles without changing the map."), Ui(), TextRender());
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	Left.HSplitTop(LazyLineSize * 1.5f, &Button, &Left);
	static CButtonContainer s_ApplyGoresVisuals;
	if(DoButton_Menu(&s_ApplyGoresVisuals, Localize("Apply Clean Gores Visuals"), 0, &Button))
	{
		g_Config.m_TcOutline = 1;
		g_Config.m_TcOutlineSolid = 0;
		g_Config.m_TcOutlineFreeze = 1;
		g_Config.m_TcOutlineUnfreeze = 1;
		g_Config.m_TcOutlineKill = 1;
		g_Config.m_TcOutlineTele = 1;
		g_Config.m_TcOutlineWidthSolid = 2;
		g_Config.m_TcOutlineWidthFreeze = 2;
		g_Config.m_TcOutlineWidthUnfreeze = 2;
		g_Config.m_TcOutlineWidthKill = 2;
		g_Config.m_TcOutlineWidthTele = 2;
		g_Config.m_TcColorFreeze = 1;
		g_Config.m_TcShowFrozenHud = 1;
		g_Config.m_TcMovingTilesEntities = 1;
		g_Config.m_ClOverlayEntities = 0;
		g_Config.m_ClShowQuads = 1;
	}
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	Left.HSplitTop(LazyLineSize, &Button, &Left);
	static CButtonContainer s_ResetGoresVisuals;
	if(DoButton_Menu(&s_ResetGoresVisuals, Localize("Reset Visual Options"), 0, &Button))
	{
		ConfigManager()->Reset("tc_outline");
		ConfigManager()->Reset("tc_outline_solid");
		ConfigManager()->Reset("tc_outline_freeze");
		ConfigManager()->Reset("tc_outline_unfreeze");
		ConfigManager()->Reset("tc_outline_kill");
		ConfigManager()->Reset("tc_outline_tele");
		ConfigManager()->Reset("tc_outline_width_solid");
		ConfigManager()->Reset("tc_outline_width_freeze");
		ConfigManager()->Reset("tc_outline_width_unfreeze");
		ConfigManager()->Reset("tc_outline_width_kill");
		ConfigManager()->Reset("tc_outline_width_tele");
		ConfigManager()->Reset("tc_color_freeze");
		ConfigManager()->Reset("tc_frozen_tees_hud");
		ConfigManager()->Reset("tc_moving_tiles_entities");
		ConfigManager()->Reset("cl_overlay_entities");
		ConfigManager()->Reset("cl_showquads");
	}

	Left.HSplitTop(LazyMargin * 1.5f, nullptr, &Left);
	LazySectionTitle(&Left, Localize("Tile Outlines"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutline, Localize("Enable outlines"), &g_Config.m_TcOutline, &Left, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineSolid, Localize("Outline hookable tiles"), &g_Config.m_TcOutlineSolid, &Left, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineFreeze, Localize("Outline freeze tiles"), &g_Config.m_TcOutlineFreeze, &Left, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineUnfreeze, Localize("Outline unfreeze tiles"), &g_Config.m_TcOutlineUnfreeze, &Left, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineKill, Localize("Outline kill tiles"), &g_Config.m_TcOutlineKill, &Left, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineTele, Localize("Outline teleporters"), &g_Config.m_TcOutlineTele, &Left, LazyLineSize);

	LazySectionTitle(&Right, Localize("Tees and Tiles"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcColorFreeze, Localize("Use skin colors for frozen tees"), &g_Config.m_TcColorFreeze, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFrozenKatana, Localize("Show katana on frozen tees"), &g_Config.m_TcFrozenKatana, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMovingTilesEntities, Localize("Show moving tiles in entities"), &g_Config.m_TcMovingTilesEntities, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHud, Localize("Show frozen tee HUD"), &g_Config.m_TcShowFrozenHud, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHudSkins, Localize("Use skins in frozen tee HUD"), &g_Config.m_TcShowFrozenHudSkins, &Right, LazyLineSize);

	Right.HSplitTop(LazyMargin * 1.5f, nullptr, &Right);
	LazySectionTitle(&Right, Localize("Map Visibility"), Ui());
	Right.HSplitTop(LazyLineSize, &Button, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_ClOverlayEntities, &g_Config.m_ClOverlayEntities, &Button, Localize("Overlay entities"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowQuads, Localize("Show background quads"), &g_Config.m_ClShowQuads, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowCenter, Localize("Show screen center lines"), &g_Config.m_TcShowCenter, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniDebug, Localize("Show position and angle"), &g_Config.m_TcMiniDebug, &Right, LazyLineSize);

	s_ScrollRegion.End();
}

void CMenus::RenderSettingsLazyChatTriggers(CUIRect MainView)
{
	CUIRect Left, Right, Button;
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	LazyBeginScroll(&MainView, &s_ScrollRegion, &ScrollParams);

	MainView.VSplitMid(&Left, &Right, 30.0f);
	Left.VSplitLeft(5.0f, nullptr, &Left);
	Right.VSplitRight(5.0f, &Right, nullptr);

	LazySectionTitle(&Left, Localize("On Connect / Join"), Ui());
	LazyInfoLabel(&Left, Localize("Commands run locally after connecting or joining a team."), Ui(), TextRender());
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	Left.HSplitTop(LazyLineSize, &Button, &Left);
	static CLineInput s_ConnectInput(g_Config.m_TcExecuteOnConnect, sizeof(g_Config.m_TcExecuteOnConnect));
	DoEditBoxWithLabel(&s_ConnectInput, &Button, Localize("On connect"), Localize("e.g. showall 1"), g_Config.m_TcExecuteOnConnect, sizeof(g_Config.m_TcExecuteOnConnect));
	Left.HSplitTop(LazyLineSize, &Button, &Left);
	static CLineInput s_JoinInput(g_Config.m_TcExecuteOnJoin, sizeof(g_Config.m_TcExecuteOnJoin));
	DoEditBoxWithLabel(&s_JoinInput, &Button, Localize("On join"), Localize("e.g. /rank"), g_Config.m_TcExecuteOnJoin, sizeof(g_Config.m_TcExecuteOnJoin));
	Left.HSplitTop(LazyLineSize, &Button, &Left);
	Ui()->DoScrollbarOption(&g_Config.m_TcExecuteOnJoinDelay, &g_Config.m_TcExecuteOnJoinDelay, &Button, Localize("Join delay"), 7, 50000, &CUi::ms_LogarithmicScrollbarScale, 0, "ticks");

	Left.HSplitTop(LazyMargin * 1.5f, nullptr, &Left);
	LazySectionTitle(&Left, Localize("Chat Filter"), Ui());
	Left.HSplitTop(LazyLineSize, &Button, &Left);
	static CLineInput s_ChatFilterInput(g_Config.m_TcRegexChatIgnore, sizeof(g_Config.m_TcRegexChatIgnore));
	DoEditBoxWithLabel(&s_ChatFilterInput, &Button, Localize("Ignore regex"), Localize("Filters chat messages"), g_Config.m_TcRegexChatIgnore, sizeof(g_Config.m_TcRegexChatIgnore));

	LazySectionTitle(&Right, Localize("Auto Reply"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMuted, Localize("Reply to muted players"), &g_Config.m_TcAutoReplyMuted, &Right, LazyLineSize);
	if(g_Config.m_TcAutoReplyMuted)
	{
		Right.HSplitTop(LazyLineSize, &Button, &Right);
		static CLineInput s_MutedReplyInput(g_Config.m_TcAutoReplyMutedMessage, sizeof(g_Config.m_TcAutoReplyMutedMessage));
		DoEditBoxWithLabel(&s_MutedReplyInput, &Button, Localize("Message"), "", g_Config.m_TcAutoReplyMutedMessage, sizeof(g_Config.m_TcAutoReplyMutedMessage));
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMinimized, Localize("Reply when tabbed out"), &g_Config.m_TcAutoReplyMinimized, &Right, LazyLineSize);
	if(g_Config.m_TcAutoReplyMinimized)
	{
		Right.HSplitTop(LazyLineSize, &Button, &Right);
		static CLineInput s_MinimizedReplyInput(g_Config.m_TcAutoReplyMinimizedMessage, sizeof(g_Config.m_TcAutoReplyMinimizedMessage));
		DoEditBoxWithLabel(&s_MinimizedReplyInput, &Button, Localize("Message"), "", g_Config.m_TcAutoReplyMinimizedMessage, sizeof(g_Config.m_TcAutoReplyMinimizedMessage));
	}

	Right.HSplitTop(LazyMargin * 1.5f, nullptr, &Right);
	LazySectionTitle(&Right, Localize("Voting"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoVoteWhenFar, Localize("Auto vote no when far"), &g_Config.m_TcAutoVoteWhenFar, &Right, LazyLineSize);
	if(g_Config.m_TcAutoVoteWhenFar)
	{
		Right.HSplitTop(LazyLineSize, &Button, &Right);
		Ui()->DoScrollbarOption(&g_Config.m_TcAutoVoteWhenFarTime, &g_Config.m_TcAutoVoteWhenFarTime, &Button, Localize("Minimum time"), 0, 20, &CUi::ms_LinearScrollbarScale, 0, " min");
		Right.HSplitTop(LazyLineSize, &Button, &Right);
		static CLineInput s_VoteMessageInput(g_Config.m_TcAutoVoteWhenFarMessage, sizeof(g_Config.m_TcAutoVoteWhenFarMessage));
		DoEditBoxWithLabel(&s_VoteMessageInput, &Button, Localize("Message"), Localize("Optional"), g_Config.m_TcAutoVoteWhenFarMessage, sizeof(g_Config.m_TcAutoVoteWhenFarMessage));
	}

	s_ScrollRegion.End();
}

void CMenus::RenderSettingsLazyMiscellaneous(CUIRect MainView)
{
	CUIRect Left, Right, Button;
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	LazyBeginScroll(&MainView, &s_ScrollRegion, &ScrollParams);

	auto OpenConfig = [this](const char *pFile) {
		char aPath[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, pFile, aPath, sizeof(aPath));
		if(!Storage()->FileExists(aPath, IStorage::TYPE_ABSOLUTE))
		{
			IOHANDLE File = Storage()->OpenFile(pFile, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(File)
				io_close(File);
		}
		Client()->ViewFile(aPath);
	};

	MainView.VSplitMid(&Left, &Right, 30.0f);
	Left.VSplitLeft(5.0f, nullptr, &Left);
	Right.VSplitRight(5.0f, &Right, nullptr);

	LazySectionTitle(&Left, Localize("Config Files"), Ui());
	LazyInfoLabel(&Left, Localize("LazyClient uses the DDNet data folder and keeps each client's settings separate."), Ui(), TextRender());
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);

	static CButtonContainer s_DDNetConfig, s_TClientConfig, s_LazyConfig;
	Left.HSplitTop(LazyLineSize * 1.5f, &Button, &Left);
	if(DoButton_Menu(&s_DDNetConfig, Localize("Open DDNet Config"), 0, &Button))
		OpenConfig("settings_ddnet.cfg");
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	Left.HSplitTop(LazyLineSize * 1.5f, &Button, &Left);
	if(DoButton_Menu(&s_TClientConfig, Localize("Open TClient Config"), 0, &Button))
		OpenConfig("settings_tclient.cfg");
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	Left.HSplitTop(LazyLineSize * 1.5f, &Button, &Left);
	if(DoButton_Menu(&s_LazyConfig, Localize("Open LazyClient Config"), 0, &Button))
		OpenConfig("settings_lazy.cfg");

	Left.HSplitTop(LazyMargin * 1.5f, nullptr, &Left);
	LazySectionTitle(&Left, Localize("Updates"), Ui());
	LazyInfoLabel(&Left, Localize("Release builds are published on GitHub."), Ui(), TextRender());
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	static CButtonContainer s_ReleasesButton;
	Left.HSplitTop(LazyLineSize * 1.5f, &Button, &Left);
	if(DoButton_Menu(&s_ReleasesButton, Localize("Open GitHub Releases"), 0, &Button))
		Client()->ViewLink("https://github.com/DASIEU/LazyClient/releases");

	LazySectionTitle(&Right, Localize("Quality of Life"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowChatClient, Localize("Show client chat messages"), &g_Config.m_TcShowChatClient, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcLimitMouseToScreen, Localize("Limit mouse to screen"), &g_Config.m_TcLimitMouseToScreen, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcScaleMouseDistance, Localize("Improve mouse precision"), &g_Config.m_TcScaleMouseDistance, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTranslateAuto, Localize("Auto-translate incoming chat"), &g_Config.m_TcTranslateAuto, &Right, LazyLineSize);

	Right.HSplitTop(LazyMargin * 1.5f, nullptr, &Right);
	LazySectionTitle(&Right, Localize("Status Bar"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBar, Localize("Enable status bar"), &g_Config.m_TcStatusBar, &Right, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBarLabels, Localize("Show status bar labels"), &g_Config.m_TcStatusBarLabels, &Right, LazyLineSize);
	Right.HSplitTop(LazyLineSize, &Button, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_TcStatusBarHeight, &g_Config.m_TcStatusBarHeight, &Button, Localize("Status bar height"), 1, 16, &CUi::ms_LinearScrollbarScale, 0, "px");

	s_ScrollRegion.End();
}

void CMenus::RenderSettingsLazyInfo(CUIRect MainView)
{
	CUIRect Left, Right, Button, Label;
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	LazyBeginScroll(&MainView, &s_ScrollRegion, &ScrollParams);

	MainView.VSplitMid(&Left, &Right, 30.0f);
	Left.VSplitLeft(5.0f, nullptr, &Left);
	Right.VSplitRight(5.0f, &Right, nullptr);

	LazySectionTitle(&Left, Localize("LazyClient"), Ui());
	Left.HSplitTop(20.0f, &Label, &Left);
	char aVersion[128];
	str_format(aVersion, sizeof(aVersion), "%s %s", CLIENT_NAME, CLIENT_RELEASE_VERSION);
	Ui()->DoLabel(&Label, aVersion, 16.0f, TEXTALIGN_ML);
	Left.HSplitTop(18.0f, &Label, &Left);
	str_format(aVersion, sizeof(aVersion), "DDNet %s", GAME_RELEASE_VERSION);
	Ui()->DoLabel(&Label, aVersion, 12.0f, TEXTALIGN_ML);
	Left.HSplitTop(18.0f, &Label, &Left);
	str_format(aVersion, sizeof(aVersion), "TClient compatibility %s", TCLIENT_VERSION);
	Ui()->DoLabel(&Label, aVersion, 12.0f, TEXTALIGN_ML);
	if(GIT_SHORTREV_HASH != nullptr && GIT_SHORTREV_HASH[0] != '\0')
	{
		Left.HSplitTop(18.0f, &Label, &Left);
		str_format(aVersion, sizeof(aVersion), "Build %s", GIT_SHORTREV_HASH);
		Ui()->DoLabel(&Label, aVersion, 12.0f, TEXTALIGN_ML);
	}

	Left.HSplitTop(LazyMargin * 1.5f, nullptr, &Left);
	LazyInfoLabel(&Left, Localize("A gores-focused DDNet/TClient fork. Local-first, fair-play, and built for the grind."), Ui(), TextRender());
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	LazyInfoLabel(&Left, Localize("Not affiliated with DDNet, TClient, or King of Gores."), Ui(), TextRender());

	Left.HSplitTop(LazyMargin * 1.5f, nullptr, &Left);
	LazySectionTitle(&Left, Localize("Links"), Ui());
	static CButtonContainer s_GithubButton, s_DDNetButton, s_TClientButton, s_KoGButton;
	Left.HSplitTop(LazyLineSize * 1.5f, &Button, &Left);
	if(DoButton_Menu(&s_GithubButton, Localize("LazyClient GitHub"), 0, &Button))
		Client()->ViewLink("https://github.com/DASIEU/LazyClient");
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	Left.HSplitTop(LazyLineSize * 1.5f, &Button, &Left);
	if(DoButton_Menu(&s_DDNetButton, Localize("DDNet"), 0, &Button))
		Client()->ViewLink("https://ddnet.org/");
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	Left.HSplitTop(LazyLineSize * 1.5f, &Button, &Left);
	if(DoButton_Menu(&s_TClientButton, Localize("TClient"), 0, &Button))
		Client()->ViewLink("https://tclient.app/");
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	Left.HSplitTop(LazyLineSize * 1.5f, &Button, &Left);
	if(DoButton_Menu(&s_KoGButton, Localize("King of Gores"), 0, &Button))
		Client()->ViewLink("https://kog.tw/");

	LazySectionTitle(&Right, Localize("Credits"), Ui());
	LazyInfoLabel(&Right, Localize("TClient by Tater and contributors."), Ui(), TextRender());
	LazyInfoLabel(&Right, Localize("DDNet by Dennis Felsing and contributors."), Ui(), TextRender());
	LazyInfoLabel(&Right, Localize("DDRace by the DDRace developers."), Ui(), TextRender());
	LazyInfoLabel(&Right, Localize("Teeworlds by Magnus Auvinen and contributors."), Ui(), TextRender());
	Right.HSplitTop(LazyMarginSmall, nullptr, &Right);
	LazyInfoLabel(&Right, Localize("LazyClient code and design by DASI."), Ui(), TextRender());

	Right.HSplitTop(LazyMargin * 1.5f, nullptr, &Right);
	LazySectionTitle(&Right, Localize("License"), Ui());
	LazyInfoLabel(&Right, Localize("LazyClient is an altered version of TClient, which is a fork of DDNet."), Ui(), TextRender());
	LazyInfoLabel(&Right, Localize("The original zlib license and third-party notices are preserved."), Ui(), TextRender());
	Right.HSplitTop(LazyMarginSmall, nullptr, &Right);
	static CButtonContainer s_LicenseButton;
	Right.HSplitTop(LazyLineSize * 1.5f, &Button, &Right);
	if(DoButton_Menu(&s_LicenseButton, Localize("Open License"), 0, &Button))
	{
		char aPath[IO_MAX_PATH_LENGTH];
		Storage()->GetBinaryPath("license.txt", aPath, sizeof(aPath));
		if(Storage()->FileExists(aPath, IStorage::TYPE_ABSOLUTE))
			Client()->ViewFile(aPath);
		else
			Client()->ViewLink("https://github.com/DASIEU/LazyClient/blob/master/license.txt");
	}

	s_ScrollRegion.End();
}
