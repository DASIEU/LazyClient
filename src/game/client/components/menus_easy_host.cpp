/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menu_background.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <chrono>
#include <vector>

#if defined(CONF_FAMILY_UNIX)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#endif

// Detects the Tailscale IPv4 address (100.64.0.0/10).
static bool GetTailscaleIPv4(char *pBuf, size_t BufSize)
{
#if defined(CONF_FAMILY_UNIX)
	struct ifaddrs *pInterfaces = nullptr;
	if(getifaddrs(&pInterfaces) != 0)
		return false;

	bool Found = false;
	for(struct ifaddrs *pInterface = pInterfaces; pInterface != nullptr; pInterface = pInterface->ifa_next)
	{
		if(pInterface->ifa_addr == nullptr || pInterface->ifa_addr->sa_family != AF_INET)
			continue;

		const auto *pAddr = reinterpret_cast<const sockaddr_in *>(pInterface->ifa_addr);
		const uint32_t Ip = ntohl(pAddr->sin_addr.s_addr);
		if((Ip & 0xFFC00000u) != 0x64400000u) // 100.64.0.0/10
			continue;

		char aAddr[INET_ADDRSTRLEN];
		if(inet_ntop(AF_INET, &pAddr->sin_addr, aAddr, sizeof(aAddr)) == nullptr)
			continue;

		str_copy(pBuf, aAddr, BufSize);
		Found = true;
		break;
	}

	freeifaddrs(pInterfaces);
	return Found;
#else
	(void)pBuf;
	(void)BufSize;
	return false;
#endif
}

static void FormatStringCommand(char *pBuf, size_t BufSize, const char *pCommand, const char *pValue)
{
	char aEscaped[512];
	char *pDst = aEscaped;
	str_escape(&pDst, pValue, aEscaped + sizeof(aEscaped) - 1);
	str_format(pBuf, BufSize, "%s \"%s\"", pCommand, aEscaped);
}

static void NormalizeHostAddress(const char *pInput, int Port, char *pBuf, size_t BufSize)
{
	str_copy(pBuf, pInput, BufSize);
	if(pBuf[0] != '\0' && str_find(pBuf, ":") == nullptr)
		str_format(pBuf, BufSize, "%s:%d", pInput, Port);
}

void CMenus::RenderEasyHost(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

	static CLineInputBuffered<128> s_ServerName;
	static CLineInputBuffered<128> s_ServerPassword;
	static CLineInputBuffered<160> s_CustomAddress;
	static CLineInputNumber s_Port;
	static bool s_Initialized = false;
	static int s_MaxPlayers = 6;
	static char s_aStatus[256] = "";
	static char s_aTailscale[64] = "";
	static std::chrono::nanoseconds s_LastAddressCheck{0};

	static CButtonContainer s_aMaxPlayerButtons[5];
	static std::vector<CButtonContainer> s_vMaxPlayerButtons(std::begin(s_aMaxPlayerButtons), std::end(s_aMaxPlayerButtons));
	static const char *s_apMaxPlayerLabels[5] = {"2", "3", "4", "5", "6"};
	static std::vector<const char *> s_vMaxPlayerLabels(std::begin(s_apMaxPlayerLabels), std::end(s_apMaxPlayerLabels));
	static std::vector<int> s_vMaxPlayerValues = {2, 3, 4, 5, 6};

	if(!s_Initialized)
	{
		s_Initialized = true;
		s_ServerName.Set(g_Config.m_SvName);
		s_ServerPassword.SetHidden(true);
		s_Port.SetInteger(g_Config.m_SvPort >= 1 && g_Config.m_SvPort <= 65535 ? g_Config.m_SvPort : 8303);
		s_MaxPlayers = std::clamp((int)g_Config.m_SvMaxClients, 2, 6);
		str_copy(s_aStatus, Localize("Ready to host."), sizeof(s_aStatus));
	}

	const auto Now = time_get_nanoseconds();
	if(Now - s_LastAddressCheck > std::chrono::seconds(2))
	{
		s_LastAddressCheck = Now;
		if(!GetTailscaleIPv4(s_aTailscale, sizeof(s_aTailscale)))
			s_aTailscale[0] = '\0';
	}

	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_ALL, 10.0f);
	MainView.Margin(20.0f, &MainView);

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 30.0f;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);

	const auto AddRow = [&](float Height, CUIRect *pRow) -> bool {
		MainView.HSplitTop(Height, pRow, &MainView);
		return s_ScrollRegion.AddRect(*pRow);
	};

	const auto AddSpacing = [&](float Height) {
		MainView.HSplitTop(Height, nullptr, &MainView);
	};

	const auto AddLabel = [&](float Height, const char *pText, float FontSize = 11.0f, int Align = TEXTALIGN_ML) {
		CUIRect Row;
		if(AddRow(Height, &Row))
			Ui()->DoLabel(&Row, pText, FontSize, Align);
	};

	// Header
	AddLabel(32.0f, Localize("Easy Host"), 24.0f);
	AddLabel(18.0f, Localize("Start a private server without port forwarding."), 11.0f);
	AddLabel(18.0f, Localize("Your home IP stays hidden. Players connect through Tailscale."), 11.0f);
	AddSpacing(10.0f);

	// Settings
	AddLabel(20.0f, Localize("Server settings"), 15.0f);
	AddSpacing(2.0f);

	{
		CUIRect Row, Label, Edit;
		if(AddRow(24.0f, &Row))
		{
			Row.VSplitLeft(150.0f, &Label, &Edit);
			Edit.VMargin(2.0f, &Edit);
			Ui()->DoLabel(&Label, Localize("Server name"), 11.0f, TEXTALIGN_ML);
			Ui()->DoEditBox(&s_ServerName, &Edit, 11.0f);
		}
	}

	{
		CUIRect Row, Label, Edit;
		if(AddRow(24.0f, &Row))
		{
			Row.VSplitLeft(150.0f, &Label, &Edit);
			Edit.VMargin(2.0f, &Edit);
			Ui()->DoLabel(&Label, Localize("Password (optional)"), 11.0f, TEXTALIGN_ML);
			Ui()->DoEditBox(&s_ServerPassword, &Edit, 11.0f);
		}
	}

	{
		CUIRect Row;
		if(AddRow(24.0f, &Row))
			DoLine_RadioMenu(Row, Localize("Max players"), s_vMaxPlayerButtons, s_vMaxPlayerLabels, s_vMaxPlayerValues, s_MaxPlayers);
	}

	{
		CUIRect Row, Label, Edit;
		if(AddRow(24.0f, &Row))
		{
			Row.VSplitLeft(150.0f, &Label, &Edit);
			Edit.VSplitLeft(90.0f, &Edit, nullptr);
			Edit.VMargin(2.0f, &Edit);
			Ui()->DoLabel(&Label, Localize("Port (UDP)"), 11.0f, TEXTALIGN_ML);
			Ui()->DoEditBox(&s_Port, &Edit, 11.0f);
		}
	}

	int Port = s_Port.GetInteger();
	if(Port < 1 || Port > 65535)
		Port = 8303;

	AddSpacing(10.0f);

	// Hosting status and start/stop button
	AddLabel(20.0f, Localize("Hosting"), 15.0f);
	AddSpacing(2.0f);

	bool ServerRunning = GameClient()->m_LocalServer.IsServerRunning();

	{
		CUIRect Row, Button, Status;
		if(AddRow(28.0f, &Row))
		{
			Row.VSplitLeft(170.0f, &Button, &Status);
			Status.VSplitLeft(12.0f, nullptr, &Status);

			static CButtonContainer s_HostButtonId;
			if(ServerRunning)
			{
				if(DoButton_Menu(&s_HostButtonId, Localize("Stop hosting"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 0.0f, 0.0f, 0.3f)))
				{
					GameClient()->m_LocalServer.KillServer();
					str_copy(s_aStatus, Localize("Server stopped."), sizeof(s_aStatus));
				}
			}
			else
			{
				if(DoButton_Menu(&s_HostButtonId, Localize("Start hosting"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 1.0f, 0.0f, 0.3f)))
				{
					char aRegister[] = "sv_register 0";
					char aPort[64];
					char aName[512];
					char aMaxClients[64];
					char aPassword[512];
					str_format(aPort, sizeof(aPort), "sv_port %d", Port);
					FormatStringCommand(aName, sizeof(aName), "sv_name", s_ServerName.GetString());
					str_format(aMaxClients, sizeof(aMaxClients), "sv_max_clients %d", s_MaxPlayers);

					std::vector<const char *> vArguments = {aRegister, aPort, aName, aMaxClients};
					if(s_ServerPassword.GetString()[0] != '\0')
					{
						FormatStringCommand(aPassword, sizeof(aPassword), "password", s_ServerPassword.GetString());
						vArguments.push_back(aPassword);
					}

					if(GameClient()->m_LocalServer.RunServer(vArguments))
					{
						str_copy(s_aStatus, Localize("Server started with master registration disabled."), sizeof(s_aStatus));
					}
					else
					{
						str_copy(s_aStatus, Localize("Server could not be started."), sizeof(s_aStatus));
					}
				}
			}

			ServerRunning = GameClient()->m_LocalServer.IsServerRunning();
			TextRender()->TextColor(ServerRunning ? ColorRGBA(0.4f, 1.0f, 0.4f, 1.0f) : ColorRGBA(0.85f, 0.85f, 0.85f, 1.0f));
			Ui()->DoLabel(&Status, ServerRunning ? Localize("Server is running") : Localize("Server is stopped"), 11.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}

	AddLabel(18.0f, s_aStatus, 10.0f);

	AddSpacing(10.0f);

	// Tunnel / share address
	AddLabel(20.0f, Localize("Share address"), 15.0f);
	AddSpacing(2.0f);

	const bool TailscaleDetected = s_aTailscale[0] != '\0';
	const bool CustomAddress = s_CustomAddress.GetString()[0] != '\0';

	{
		CUIRect Row, Status, RefreshButton, DownloadButton;
		if(AddRow(24.0f, &Row))
		{
			Row.VSplitRight(220.0f, &Status, &DownloadButton);
			DownloadButton.VSplitLeft(6.0f, nullptr, &DownloadButton);
			DownloadButton.VSplitLeft(90.0f, &RefreshButton, &DownloadButton);
			DownloadButton.VSplitLeft(6.0f, nullptr, &DownloadButton);

			char aText[256];
			if(CustomAddress)
				str_copy(aText, Localize("Custom address will be used for the invite."), sizeof(aText));
			else if(TailscaleDetected)
				str_format(aText, sizeof(aText), Localize("Tailscale detected: %s"), s_aTailscale);
			else
				str_copy(aText, Localize("Tailscale was not detected on this PC."), sizeof(aText));

			TextRender()->TextColor(CustomAddress || TailscaleDetected ? ColorRGBA(0.4f, 1.0f, 0.4f, 1.0f) : ColorRGBA(1.0f, 0.6f, 0.4f, 1.0f));
			Ui()->DoLabel(&Status, aText, 11.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			static CButtonContainer s_RefreshAddressId;
			if(DoButton_Menu(&s_RefreshAddressId, Localize("Refresh"), 0, &RefreshButton))
			{
				if(!GetTailscaleIPv4(s_aTailscale, sizeof(s_aTailscale)))
					s_aTailscale[0] = '\0';
				s_LastAddressCheck = time_get_nanoseconds();
			}

			static CButtonContainer s_DownloadTailscaleId;
			if(DoButton_Menu(&s_DownloadTailscaleId, Localize("Get Tailscale"), 0, &DownloadButton))
				Client()->ViewLink("https://tailscale.com/download");
		}
	}

	{
		CUIRect Row, Label, Edit;
		if(AddRow(24.0f, &Row))
		{
			Row.VSplitLeft(150.0f, &Label, &Edit);
			Edit.VMargin(2.0f, &Edit);
			Ui()->DoLabel(&Label, Localize("Custom address (optional)"), 11.0f, TEXTALIGN_ML);
			Ui()->DoEditBox(&s_CustomAddress, &Edit, 11.0f);
		}
	}

	char aHost[192];
	if(CustomAddress)
		NormalizeHostAddress(s_CustomAddress.GetString(), Port, aHost, sizeof(aHost));
	else if(TailscaleDetected)
		str_format(aHost, sizeof(aHost), "%s:%d", s_aTailscale, Port);
	else
		aHost[0] = '\0';

	if(ServerRunning && aHost[0] != '\0')
	{
		char aInvite[256];
		str_format(aInvite, sizeof(aInvite), "connect %s", aHost);

		AddSpacing(6.0f);
		AddLabel(18.0f, Localize("Send this invite to the players:"), 10.0f);
		AddLabel(22.0f, aInvite, 13.0f, TEXTALIGN_ML);

		CUIRect Row, CopyButton, JoinButton;
		if(AddRow(28.0f, &Row))
		{
			Row.VSplitLeft(150.0f, &CopyButton, &Row);
			Row.VSplitLeft(8.0f, nullptr, &Row);
			Row.VSplitLeft(150.0f, &JoinButton, nullptr);

			static CButtonContainer s_CopyInviteId;
			if(DoButton_Menu(&s_CopyInviteId, Localize("Copy invite"), 0, &CopyButton))
			{
				Input()->SetClipboardText(aInvite);
				str_copy(s_aStatus, Localize("Invite copied to clipboard."), sizeof(s_aStatus));
			}

			static CButtonContainer s_JoinLocalId;
			if(DoButton_Menu(&s_JoinLocalId, Localize("Join locally"), 0, &JoinButton))
			{
				char aLocalAddress[64];
				str_format(aLocalAddress, sizeof(aLocalAddress), "127.0.0.1:%d", Port);
				const char *pPassword = s_ServerPassword.GetString();
				Client()->Connect(aLocalAddress, pPassword[0] != '\0' ? pPassword : nullptr);
			}
		}
	}

	if(!TailscaleDetected && !CustomAddress)
	{
		AddSpacing(8.0f);
		AddLabel(18.0f, Localize("How to set it up:"), 12.0f);
		AddLabel(18.0f, Localize("1. Install Tailscale on this PC and sign in."), 10.0f);
		AddLabel(18.0f, Localize("2. Install Tailscale on each player's PC and sign in."), 10.0f);
		AddLabel(18.0f, Localize("3. Share this PC with them in the Tailscale admin console."), 10.0f);
		AddLabel(18.0f, Localize("4. Click Refresh above and start hosting."), 10.0f);
	}

	AddSpacing(8.0f);
	AddLabel(18.0f, Localize("You can also enter a playit.gg or other tunnel address above."), 10.0f);
	AddLabel(18.0f, Localize("The server never appears on the public master list."), 10.0f);

	s_ScrollRegion.End();
}
