#include <base/io.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/components/lazy/lazy.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>
#include <numeric>

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

static void LazyEndScroll(CScrollRegion *pScrollRegion, CUIRect ContentBottom)
{
	// Register the consumed content height so the scrollbar can actually
	// reach the lower sections of the LazyClient settings pages.
	ContentBottom.y += 10.0f;
	ContentBottom.h = 1.0f;
	pScrollRegion->AddRect(ContentBottom);
	pScrollRegion->End();
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
	CUIRect Left, Right, Button, Tabs;
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	LazyBeginScroll(&MainView, &s_ScrollRegion, &ScrollParams);

	MainView.VSplitMid(&Left, &Right, 30.0f);
	Left.VSplitLeft(5.0f, nullptr, &Left);
	Right.VSplitRight(5.0f, &Right, nullptr);

	// ---------------------------------------------------------------- Input modes
	LazySectionTitle(&Left, Localize("Input Modes"), Ui());
	LazyInfoLabel(&Left, Localize("Shifts input prediction forward. The higher the amount, the earlier your tee reacts."), Ui(), TextRender());
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);

	Left.HSplitTop(LazyLineSize, &Tabs, &Left);
	static const char *s_apInputModes[] = {"Off", "Fast", "Best", "Saiko", "Cloud"};
	static CButtonContainer s_aInputModeButtons[5] = {};
	const float InputModeWidth = Tabs.w / 5.0f;
	for(int Mode = 0; Mode < 5; ++Mode)
	{
		CUIRect Tab;
		Tabs.VSplitLeft(InputModeWidth, &Tab, &Tabs);
		const int Corners = Mode == 0 ? IGraphics::CORNER_L : Mode == 4 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE;
		if(DoButton_MenuTab(&s_aInputModeButtons[Mode], Localize(s_apInputModes[Mode]), g_Config.m_LcInputMode == Mode, &Tab, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			g_Config.m_LcInputMode = Mode;
	}
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);

	if(g_Config.m_LcInputMode == 1)
	{
		Left.HSplitTop(LazyLineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_LcFastInputAmount, &g_Config.m_LcFastInputAmount, &Button, Localize("Fast amount"), 0, 200, &CUi::ms_LinearScrollbarScale, 0, "ms");
	}
	else if(g_Config.m_LcInputMode == 2)
	{
		Left.HSplitTop(LazyLineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_LcBestInputAmount, &g_Config.m_LcBestInputAmount, &Button, Localize("Best amount"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "0.01 ticks");
		Left.HSplitTop(LazyLineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_LcBestInputSmoothing, &g_Config.m_LcBestInputSmoothing, &Button, Localize("Smoothing"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Left.HSplitTop(LazyLineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_LcBestInputLatencyComp, &g_Config.m_LcBestInputLatencyComp, &Button, Localize("Latency compensation"), 0, 50, &CUi::ms_LinearScrollbarScale, 0, "%");
	}
	else if(g_Config.m_LcInputMode == 3)
	{
		Left.HSplitTop(LazyLineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_LcSaikoInputAmount, &g_Config.m_LcSaikoInputAmount, &Button, Localize("Saiko amount"), 0, 500, &CUi::ms_LinearScrollbarScale, 0, "0.01 ticks");
	}
	else if(g_Config.m_LcInputMode == 4)
	{
		Left.HSplitTop(LazyLineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_LcCloudInputAmount, &g_Config.m_LcCloudInputAmount, &Button, Localize("Cloud amount"), 0, 500, &CUi::ms_LinearScrollbarScale, 0, "0.01 ticks");
	}

	if(g_Config.m_LcInputMode != 0)
	{
		Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcInputOthers, Localize("Apply to other tees"), &g_Config.m_LcInputOthers, &Left, LazyLineSize);
	}

	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	Left.HSplitTop(LazyLineSize, &Button, &Left);
	static CButtonContainer s_ResetInputModeButton;
	if(DoButton_Menu(&s_ResetInputModeButton, Localize("Reset input mode settings"), 0, &Button))
	{
		ConfigManager()->Reset("lc_input_mode");
		ConfigManager()->Reset("lc_input_others");
		ConfigManager()->Reset("lc_fast_input_amount");
		ConfigManager()->Reset("lc_best_input_amount");
		ConfigManager()->Reset("lc_best_input_smoothing");
		ConfigManager()->Reset("lc_best_input_latency_comp");
		ConfigManager()->Reset("lc_saiko_input_amount");
		ConfigManager()->Reset("lc_cloud_input_amount");
		ConfigManager()->Reset("tc_fast_input");
		ConfigManager()->Reset("tc_fast_input_others");
	}

	// ---------------------------------------------------------------- Gores mode
	Left.HSplitTop(LazyMargin * 1.5f, nullptr, &Left);
	LazySectionTitle(&Left, Localize("Gores Mode"), Ui());
	LazyInfoLabel(&Left, Localize("Holds the gun. Left click switches to the hammer, hits, then switches back after the delay."), Ui(), TextRender());
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcGoresMode, Localize("Enable gores mode"), &g_Config.m_LcGoresMode, &Left, LazyLineSize);

	if(g_Config.m_LcGoresMode)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcGoresIgnoreHeavy, Localize("Ignore shotgun, grenade, laser and ninja"), &g_Config.m_LcGoresIgnoreHeavy, &Left, LazyLineSize);
		Left.HSplitTop(LazyLineSize, &Button, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_LcGoresSwitchDelay, &g_Config.m_LcGoresSwitchDelay, &Button, Localize("Switch back delay"), 0, 500, &CUi::ms_LinearScrollbarScale, 0, "ms");
		Left.HSplitTop(LazyLineSize, &Button, &Left);
		static CLineInput s_GunKeyInput(g_Config.m_LcGoresGunKey, sizeof(g_Config.m_LcGoresGunKey));
		DoEditBoxWithLabel(&s_GunKeyInput, &Button, Localize("Gun fire key"), Localize("e.g. w"), g_Config.m_LcGoresGunKey, sizeof(g_Config.m_LcGoresGunKey));
		Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
		Left.HSplitTop(LazyLineSize, &Button, &Left);
		static CButtonContainer s_BindGunKeyButton;
		if(DoButton_Menu(&s_BindGunKeyButton, Localize("Apply gun key bind"), 0, &Button))
			GameClient()->m_Lazy.BindGunKey();
	}

	// ---------------------------------------------------------------- Right column
	LazySectionTitle(&Right, Localize("Antiping"), Ui());

	Right.HSplitTop(LazyLineSize, &Tabs, &Right);
	static const char *s_apAntipingModes[] = {"Off", "On", "Auto"};
	static CButtonContainer s_aAntipingModeButtons[3] = {};
	const float AntipingModeWidth = Tabs.w / 3.0f;
	for(int Mode = 0; Mode < 3; ++Mode)
	{
		CUIRect Tab;
		Tabs.VSplitLeft(AntipingModeWidth, &Tab, &Tabs);
		const int Corners = Mode == 0 ? IGraphics::CORNER_L : Mode == 2 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE;
		if(DoButton_MenuTab(&s_aAntipingModeButtons[Mode], Localize(s_apAntipingModes[Mode]), g_Config.m_LcAntipingMode == Mode, &Tab, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			g_Config.m_LcAntipingMode = Mode;
	}

	if(g_Config.m_LcAntipingMode == 2)
	{
		Right.HSplitTop(LazyMarginSmall, nullptr, &Right);
		Right.HSplitTop(LazyLineSize, &Button, &Right);
		CUIRect Label, EditBox;
		Button.VSplitLeft(130.0f, &Label, &Button);
		Button.VSplitLeft(80.0f, &EditBox, nullptr);
		Ui()->DoLabel(&Label, Localize("Ping threshold"), 14.0f, TEXTALIGN_ML);

		static CLineInputNumber s_AntipingPing(60);
		if(!s_AntipingPing.IsActive() && s_AntipingPing.GetInteger() != g_Config.m_LcAntipingAutoPing)
			s_AntipingPing.SetInteger(g_Config.m_LcAntipingAutoPing);
		Ui()->DoEditBox(&s_AntipingPing, &EditBox, 12.0f);
		if(!s_AntipingPing.IsEmpty())
		{
			const int Value = std::clamp(s_AntipingPing.GetInteger(), 1, 1000);
			if(Value != g_Config.m_LcAntipingAutoPing)
				g_Config.m_LcAntipingAutoPing = Value;
		}
		LazyInfoLabel(&Right, Localize("Turns antiping on when ping is at or above this value, and off when it is below."), Ui(), TextRender());
	}
	else if(g_Config.m_LcAntipingMode == 0)
	{
		LazyInfoLabel(&Right, Localize("Antiping is turned off."), Ui(), TextRender());
	}
	else
	{
		LazyInfoLabel(&Right, Localize("Antiping is always on."), Ui(), TextRender());
	}

	Right.HSplitTop(LazyMarginSmall, nullptr, &Right);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcAutoMargin, Localize("Automatic prediction margin"), &g_Config.m_LcAutoMargin, &Right, LazyLineSize);
	LazyInfoLabel(&Right, Localize("Raises the prediction margin when ping or jitter gets high."), Ui(), TextRender());

	Right.HSplitTop(LazyMargin * 1.5f, nullptr, &Right);
	LazySectionTitle(&Right, Localize("Snap Tap"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcSnapTap, Localize("Enable snap tap"), &g_Config.m_LcSnapTap, &Right, LazyLineSize);
	if(g_Config.m_LcSnapTap)
	{
		Right.HSplitTop(LazyLineSize, &Button, &Right);
		Ui()->DoScrollbarOption(&g_Config.m_LcSnapTapDelay, &g_Config.m_LcSnapTapDelay, &Button, Localize("Switch delay"), 0, 200, &CUi::ms_LinearScrollbarScale, 0, "ms");
	}

	Right.HSplitTop(LazyMargin * 1.5f, nullptr, &Right);
	LazySectionTitle(&Right, Localize("Aspect Ratio"), Ui());
	LazyInfoLabel(&Right, Localize("Stretches the game to fill the window at the chosen ratio. Reverts after 5 seconds unless confirmed."), Ui(), TextRender());
	Right.HSplitTop(LazyMarginSmall, nullptr, &Right);

	static CLineInputNumber s_AspectWidth(16);
	static CLineInputNumber s_AspectHeight(9);
	static int s_LastAspectWidth = -1;
	static int s_LastAspectHeight = -1;
	if(!s_AspectWidth.IsActive() && !s_AspectHeight.IsActive() &&
		(g_Config.m_LcAspectWidth != s_LastAspectWidth || g_Config.m_LcAspectHeight != s_LastAspectHeight))
	{
		s_AspectWidth.SetInteger(g_Config.m_LcAspectWidth);
		s_AspectHeight.SetInteger(g_Config.m_LcAspectHeight);
		s_LastAspectWidth = g_Config.m_LcAspectWidth;
		s_LastAspectHeight = g_Config.m_LcAspectHeight;
	}

	Right.HSplitTop(LazyLineSize, &Button, &Right);
	CUIRect WidthLabel, WidthBox, HeightLabel, HeightBox;
	Button.VSplitLeft(55.0f, &WidthLabel, &Button);
	Button.VSplitLeft(65.0f, &WidthBox, &Button);
	Button.VSplitLeft(10.0f, nullptr, &Button);
	Button.VSplitLeft(55.0f, &HeightLabel, &Button);
	Button.VSplitLeft(65.0f, &HeightBox, &Button);
	Ui()->DoLabel(&WidthLabel, Localize("Width"), 14.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&HeightLabel, Localize("Height"), 14.0f, TEXTALIGN_ML);
	Ui()->DoEditBox(&s_AspectWidth, &WidthBox, 12.0f);
	Ui()->DoEditBox(&s_AspectHeight, &HeightBox, 12.0f);

	Right.HSplitTop(LazyMarginSmall, nullptr, &Right);
	CUIRect ApplyButton, OffButton;
	Right.HSplitTop(LazyLineSize * 1.5f, &ApplyButton, &Right);
	ApplyButton.VSplitMid(&ApplyButton, &OffButton, 5.0f);
	static CButtonContainer s_ApplyAspectButton, s_OffAspectButton;
	if(DoButton_Menu(&s_ApplyAspectButton, Localize("Apply"), 0, &ApplyButton))
	{
		const int W = s_AspectWidth.GetInteger();
		const int H = s_AspectHeight.GetInteger();
		if(W > 0 && H > 0)
		{
			g_Config.m_LcAspectWidth = std::clamp(W, 1, 10000);
			g_Config.m_LcAspectHeight = std::clamp(H, 1, 10000);
		}
	}
	if(DoButton_Menu(&s_OffAspectButton, Localize("Off"), 0, &OffButton))
	{
		g_Config.m_LcAspectWidth = 0;
		g_Config.m_LcAspectHeight = 0;
	}

	if(g_Config.m_LcAspectWidth > 0 && g_Config.m_LcAspectHeight > 0)
	{
		const int Gcd = std::gcd(g_Config.m_LcAspectWidth, g_Config.m_LcAspectHeight);
		char aRatio[128];
		if(Gcd > 0)
			str_format(aRatio, sizeof(aRatio), Localize("Current: %d:%d (%.3f)"), g_Config.m_LcAspectWidth / Gcd, g_Config.m_LcAspectHeight / Gcd, (float)g_Config.m_LcAspectWidth / (float)g_Config.m_LcAspectHeight);
		else
			str_format(aRatio, sizeof(aRatio), Localize("Current: %d:%d"), g_Config.m_LcAspectWidth, g_Config.m_LcAspectHeight);
		LazyInfoLabel(&Right, aRatio, Ui(), TextRender());
	}
	else
	{
		LazyInfoLabel(&Right, Localize("Off. Enter two positive numbers, for example 16 and 9."), Ui(), TextRender());
	}

	if(GameClient()->m_Lazy.AspectConfirmActive())
	{
		char aConfirm[128];
		str_format(aConfirm, sizeof(aConfirm), Localize("Keep this aspect ratio? Reverting in %d..."), GameClient()->m_Lazy.AspectSecondsLeft());
		LazyInfoLabel(&Right, aConfirm, Ui(), TextRender());
		Right.HSplitTop(LazyMarginSmall, nullptr, &Right);

		CUIRect KeepButton, RevertButton;
		Right.HSplitTop(LazyLineSize * 1.5f, &KeepButton, &Right);
		KeepButton.VSplitMid(&KeepButton, &RevertButton, 5.0f);
		static CButtonContainer s_KeepAspectButton, s_RevertAspectButton;
		if(DoButton_Menu(&s_KeepAspectButton, Localize("Keep"), 0, &KeepButton))
			GameClient()->m_Lazy.KeepAspect();
		if(DoButton_Menu(&s_RevertAspectButton, Localize("Revert"), 0, &RevertButton))
			GameClient()->m_Lazy.RevertAspect();
	}

	CUIRect ContentBottom = Left;
	ContentBottom.y = std::max(Left.y, Right.y);
	LazyEndScroll(&s_ScrollRegion, ContentBottom);
}

void CMenus::RenderSettingsLazyVisuals(CUIRect MainView)
{
	CUIRect Button;
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	LazyBeginScroll(&MainView, &s_ScrollRegion, &ScrollParams);

	LazySectionTitle(&MainView, Localize("Client Logos"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcClientLogos, Localize("Show client logos"), &g_Config.m_LcClientLogos, &MainView, LazyLineSize);
	LazyInfoLabel(&MainView, Localize("Shows a small logo before player names. LazyClient also tags its own skin so compatible clients can show it."), Ui(), TextRender());
	if(g_Config.m_LcClientLogos)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcBestClientPublicList, Localize("Use BestClient public list (network)"), &g_Config.m_LcBestClientPublicList, &MainView, LazyLineSize);
		LazyInfoLabel(&MainView, Localize("Fetches BestClient's public player list to show its logo. Disable this to stay fully local."), Ui(), TextRender());
	}

	MainView.HSplitTop(LazyMargin * 1.5f, nullptr, &MainView);
	LazySectionTitle(&MainView, Localize("Scoreboard"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcFriendHeartScoreboard, Localize("Friend heart"), &g_Config.m_LcFriendHeartScoreboard, &MainView, LazyLineSize);
	LazyInfoLabel(&MainView, Localize("Shows a heart before friends in the scoreboard."), Ui(), TextRender());

	MainView.HSplitTop(LazyMargin * 1.5f, nullptr, &MainView);
	LazySectionTitle(&MainView, Localize("Votes"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcBetterVotes, Localize("Better vote display"), &g_Config.m_LcBetterVotes, &MainView, LazyLineSize);
	if(g_Config.m_LcBetterVotes)
	{
		MainView.HSplitTop(LazyLineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_LcVotePassPercentage, &g_Config.m_LcVotePassPercentage, &Button, Localize("Required yes percentage"), 1, 99, &CUi::ms_LinearScrollbarScale, 0, "%");
		LazyInfoLabel(&MainView, Localize("Match this to the server's sv_vote_yes_percentage setting. The vote list shows the remaining yes votes."), Ui(), TextRender());
	}
	else
	{
		LazyInfoLabel(&MainView, Localize("Vote percentages, the pass threshold and remaining yes votes are hidden."), Ui(), TextRender());
	}

	MainView.HSplitTop(LazyMargin * 1.5f, nullptr, &MainView);
	LazySectionTitle(&MainView, Localize("Hooked Freeze Predictor"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcZeroGravPredict, Localize("Predict freeze tiles"), &g_Config.m_LcZeroGravPredict, &MainView, LazyLineSize);
	if(g_Config.m_LcZeroGravPredict)
	{
		MainView.HSplitTop(LazyLineSize, &Button, &MainView);
		static CUi::SDropDownState s_ZeroGravModeDropDownState;
		static CScrollRegion s_ZeroGravModeDropDownScrollRegion;
		s_ZeroGravModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ZeroGravModeDropDownScrollRegion;
		const char *apZeroGravModes[] = {Localize("Prediction"), Localize("Forced")};
		const int NewZeroGravMode = Ui()->DoDropDown(&Button, g_Config.m_LcZeroGravMode, apZeroGravModes, 2, s_ZeroGravModeDropDownState);
		if(NewZeroGravMode != g_Config.m_LcZeroGravMode)
			g_Config.m_LcZeroGravMode = NewZeroGravMode;

		MainView.HSplitTop(LazyLineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_LcZeroGravPredictTime, &g_Config.m_LcZeroGravPredictTime, &Button, Localize("Warning time"), 1, 10, &CUi::ms_LinearScrollbarScale, 0, "s");

		if(g_Config.m_LcZeroGravMode == 0)
		{
			MainView.HSplitTop(LazyLineSize, &Button, &MainView);
			Ui()->DoScrollbarOption(&g_Config.m_LcZeroGravHoldTiles, &g_Config.m_LcZeroGravHoldTiles, &Button, Localize("Hold area radius"), 1, 16, &CUi::ms_LinearScrollbarScale, 0, " tiles");

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcZeroGravSpeedrunGuard, Localize("Anti-speedrun guard"), &g_Config.m_LcZeroGravSpeedrunGuard, &MainView, LazyLineSize);
			if(g_Config.m_LcZeroGravSpeedrunGuard)
			{
				MainView.HSplitTop(LazyLineSize, &Button, &MainView);
				Ui()->DoScrollbarOption(&g_Config.m_LcZeroGravSpeedrunSpeed, &g_Config.m_LcZeroGravSpeedrunSpeed, &Button, Localize("Speedrun guard speed"), 5, 60, &CUi::ms_LinearScrollbarScale, 0, " tiles/s");
			}

			LazyInfoLabel(&MainView, Localize("Only while holding the opposite key against a wall hook and staying inside the hold area for two seconds. The warning stays visible for 0.2 s after the current prediction disappears, which removes short flashes. The anti-speedrun guard hides it while you move faster than the speed threshold; lower the threshold to filter speedruns more aggressively. Works for your tee, a followed player, and the tee nearest the camera in freeview."), Ui(), TextRender());
		}
		else
		{
			MainView.HSplitTop(LazyLineSize, &Button, &MainView);
			static CLineInput s_ForcedKeyInput(g_Config.m_LcZeroGravForcedKey, sizeof(g_Config.m_LcZeroGravForcedKey));
			DoEditBoxWithLabel(&s_ForcedKeyInput, &Button, Localize("Forced mode key"), Localize("e.g. x"), g_Config.m_LcZeroGravForcedKey, sizeof(g_Config.m_LcZeroGravForcedKey));
			MainView.HSplitTop(LazyMarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LazyLineSize, &Button, &MainView);
			static CButtonContainer s_BindForcedKeyButton;
			if(DoButton_Menu(&s_BindForcedKeyButton, Localize("Apply forced key bind"), 0, &Button))
				GameClient()->m_Lazy.BindZeroGravKey();

			LazyInfoLabel(&MainView, Localize("Press the bound key at a wall to save the current hold. The client sends say /spec, waits 1 s, sends it again, then shows the warning if the saved hold reaches freeze within the warning time. The hold-area and speedrun checks are skipped in forced mode. Any new key or mouse press after the second say /spec turns it off; input during the one-second wait is ignored. Keys are ignored in chat and mouse clicks are ignored on the scoreboard."), Ui(), TextRender());
		}
	}
	else
	{
		LazyInfoLabel(&MainView, Localize("No hooked freeze warning is shown."), Ui(), TextRender());
	}

	MainView.HSplitTop(LazyMargin * 1.5f, nullptr, &MainView);
	LazySectionTitle(&MainView, Localize("Telemetry"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcTelemetry, Localize("Show telemetry overlay"), &g_Config.m_LcTelemetry, &MainView, LazyLineSize);
	LazyInfoLabel(&MainView, Localize("Choose which values the overlay shows."), Ui(), TextRender());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcTelemetryFps, Localize("FPS"), &g_Config.m_LcTelemetryFps, &MainView, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcTelemetryFpsAvg, Localize("Average FPS"), &g_Config.m_LcTelemetryFpsAvg, &MainView, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcTelemetryFps1Pct, Localize("1% low FPS"), &g_Config.m_LcTelemetryFps1Pct, &MainView, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcTelemetryFps01Pct, Localize("0.1% low FPS"), &g_Config.m_LcTelemetryFps01Pct, &MainView, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcTelemetryFrametime, Localize("Frame time"), &g_Config.m_LcTelemetryFrametime, &MainView, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcTelemetryPing, Localize("Ping"), &g_Config.m_LcTelemetryPing, &MainView, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcTelemetryJitter, Localize("Ping jitter"), &g_Config.m_LcTelemetryJitter, &MainView, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcTelemetryLoss, Localize("Packet loss"), &g_Config.m_LcTelemetryLoss, &MainView, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcTelemetryPrediction, Localize("Prediction time"), &g_Config.m_LcTelemetryPrediction, &MainView, LazyLineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcTelemetrySnaps, Localize("Snapshots per second"), &g_Config.m_LcTelemetrySnaps, &MainView, LazyLineSize);
	LazyInfoLabel(&MainView, Localize("Local-only overlay in the top-right. Packet loss is estimated from missing vital network chunks and resend requests over the last few seconds."), Ui(), TextRender());

	LazyEndScroll(&s_ScrollRegion, MainView);
}

void CMenus::RenderSettingsLazyChatTriggers(CUIRect MainView)
{
	CUIRect Label;
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	LazyBeginScroll(&MainView, &s_ScrollRegion, &ScrollParams);

	LazySectionTitle(&MainView, Localize("Chat Triggers"), Ui());
	MainView.HSplitTop(20.0f, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("No options yet."), 14.0f, TEXTALIGN_ML);

	LazyEndScroll(&s_ScrollRegion, MainView);
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

	LazySectionTitle(&Right, Localize("Updates"), Ui());
	LazyInfoLabel(&Right, Localize("Release builds are published on GitHub."), Ui(), TextRender());
	Right.HSplitTop(LazyMarginSmall, nullptr, &Right);
	static CButtonContainer s_ReleasesButton;
	Right.HSplitTop(LazyLineSize * 1.5f, &Button, &Right);
	if(DoButton_Menu(&s_ReleasesButton, Localize("Open GitHub Releases"), 0, &Button))
		Client()->ViewLink("https://github.com/DASIEU/LazyClient/releases");

	CUIRect ContentBottom = Left;
	ContentBottom.y = std::max(Left.y, Right.y);
	LazyEndScroll(&s_ScrollRegion, ContentBottom);
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

	CUIRect ContentBottom = Left;
	ContentBottom.y = std::max(Left.y, Right.y);
	LazyEndScroll(&s_ScrollRegion, ContentBottom);
}
