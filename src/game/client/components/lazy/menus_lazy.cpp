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

	// ---------------------------------------------------------------- Gores mode
	Left.HSplitTop(LazyMargin * 1.5f, nullptr, &Left);
	LazySectionTitle(&Left, Localize("Gores Mode"), Ui());
	LazyInfoLabel(&Left, Localize("Holds the gun. Firing switches to the hammer and back after the delay."), Ui(), TextRender());
	Left.HSplitTop(LazyMarginSmall, nullptr, &Left);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcGoresMode, Localize("Enable gores mode"), &g_Config.m_LcGoresMode, &Left, LazyLineSize);

	if(g_Config.m_LcGoresMode)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcGoresDisableIfWeapons, Localize("Ignore shotgun, grenade, laser and ninja"), &g_Config.m_LcGoresDisableIfWeapons, &Left, LazyLineSize);
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
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcAutoMargin, Localize("Automatic prediction margin"), &g_Config.m_LcAutoMargin, &Right, LazyLineSize);
	LazyInfoLabel(&Right, Localize("Raises the prediction margin when ping or jitter gets high."), Ui(), TextRender());

	Right.HSplitTop(LazyMargin * 1.5f, nullptr, &Right);
	LazySectionTitle(&Right, Localize("Snap Tap"), Ui());
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_LcSnapTap, Localize("Enable snap tap"), &g_Config.m_LcSnapTap, &Right, LazyLineSize);
	if(g_Config.m_LcSnapTap)
	{
		Right.HSplitTop(LazyLineSize, &Button, &Right);
		Ui()->DoScrollbarOption(&g_Config.m_LcSnapTapDelay, &g_Config.m_LcSnapTapDelay, &Button, Localize("Switch delay"), 0, 200, &CUi::ms_LinearScrollbarScale, 0, "ms");
		Right.HSplitTop(LazyLineSize, &Button, &Right);
		Ui()->DoScrollbarOption(&g_Config.m_LcSnapTapSmoothness, &g_Config.m_LcSnapTapSmoothness, &Button, Localize("Release smoothing"), 0, 200, &CUi::ms_LinearScrollbarScale, 0, "ms");
	}

	Right.HSplitTop(LazyMargin * 1.5f, nullptr, &Right);
	LazySectionTitle(&Right, Localize("Custom Aspect Ratio"), Ui());
	Right.HSplitTop(LazyLineSize, &Button, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_LcCustomAspect, &g_Config.m_LcCustomAspect, &Button, Localize("Aspect ratio x100"), 0, 400, &CUi::ms_LinearScrollbarScale, 0, "");
	if(g_Config.m_LcCustomAspect > 0)
	{
		char aRatio[64];
		str_format(aRatio, sizeof(aRatio), Localize("Current: %.2f:1"), g_Config.m_LcCustomAspect / 100.0f);
		LazyInfoLabel(&Right, aRatio, Ui(), TextRender());
	}
	else
	{
		LazyInfoLabel(&Right, Localize("0 = off. 125 = 5:4, 133 = 4:3, 150 = 3:2, 177 = 16:9."), Ui(), TextRender());
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

	s_ScrollRegion.End();
}

void CMenus::RenderSettingsLazyVisuals(CUIRect MainView)
{
	CUIRect Label;
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	LazyBeginScroll(&MainView, &s_ScrollRegion, &ScrollParams);

	LazySectionTitle(&MainView, Localize("Visuals"), Ui());
	MainView.HSplitTop(20.0f, &Label, &MainView);
	Ui()->DoLabel(&Label, Localize("No options yet."), 14.0f, TEXTALIGN_ML);

	s_ScrollRegion.End();
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

	LazySectionTitle(&Right, Localize("Updates"), Ui());
	LazyInfoLabel(&Right, Localize("Release builds are published on GitHub."), Ui(), TextRender());
	Right.HSplitTop(LazyMarginSmall, nullptr, &Right);
	static CButtonContainer s_ReleasesButton;
	Right.HSplitTop(LazyLineSize * 1.5f, &Button, &Right);
	if(DoButton_Menu(&s_ReleasesButton, Localize("Open GitHub Releases"), 0, &Button))
		Client()->ViewLink("https://github.com/DASIEU/LazyClient/releases");

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
