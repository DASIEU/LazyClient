#include "lazy.h"

#include "lazy_ccid.h"

#include <base/log.h>
#include <base/math.h>
#include <base/net.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/components/binds.h>
#include <game/client/components/controls.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr int AUTO_MARGIN_BASE = 9;
constexpr int AUTO_MARGIN_MAX = 300;
constexpr float AUTO_MARGIN_CHECK_INTERVAL = 0.5f;
constexpr float AUTO_MARGIN_PING_SMOOTH_RISE = 0.45f;
constexpr float AUTO_MARGIN_PING_SMOOTH_FALL = 0.20f;
constexpr float AUTO_MARGIN_JITTER_SMOOTH_RISE = 0.35f;
constexpr float AUTO_MARGIN_JITTER_SMOOTH_FALL = 0.15f;
constexpr int AUTO_MARGIN_HIGH_PING_ENTER = 50;
constexpr int AUTO_MARGIN_HIGH_PING_EXIT = 42;
constexpr float AUTO_MARGIN_JITTER_ENTER = 4.0f;
constexpr float AUTO_MARGIN_JITTER_EXIT = 2.0f;
constexpr int AUTO_MARGIN_DEADZONE = 1;
constexpr int AUTO_MARGIN_STEP_DOWN = 5;
constexpr int ASPECT_CONFIRM_SECONDS = 5;
constexpr int64_t GORES_WEAPON_SELECT_GRACE_MS = 500;
constexpr float ZERO_GRAV_TILE_SIZE = 32.0f;
constexpr int ZERO_GRAV_HOLD_GRACE_CHECKS = 10;
constexpr float ZERO_GRAV_HOOK_MOVE_DISTANCE = 16.0f;
constexpr float ZERO_GRAV_WARNING_HIDE_DELAY = 0.20f;
constexpr int ZERO_GRAV_SPEEDRUN_FAST_CHECKS = 3;
constexpr int ZERO_GRAV_SPEEDRUN_COOLDOWN_CHECKS = 5;
constexpr float ZERO_GRAV_FORCED_SPEC_DELAY = 1.00f;
constexpr float ZERO_GRAV_FORCED_CANDIDATE_AGE = 1.00f;
constexpr int ZERO_GRAV_FORCED_OFF = 0;
constexpr int ZERO_GRAV_FORCED_WAIT_SECOND = 1;
constexpr int ZERO_GRAV_FORCED_ACTIVE = 2;
constexpr float TELEMETRY_STATS_INTERVAL = 1.0f;

constexpr const char *BESTCLIENT_USERS_URL = "https://150.241.70.188:8779/users.json";
constexpr float BESTCLIENT_LIST_REFRESH_SECONDS = 60.0f;
constexpr float BESTCLIENT_LIST_RETRY_SECONDS = 10.0f;
constexpr int64_t BESTCLIENT_LIST_MAX_RESPONSE_SIZE = 4 * 1024 * 1024;

bool IsHeavyWeapon(int Weapon)
{
	return Weapon == WEAPON_SHOTGUN || Weapon == WEAPON_GRENADE || Weapon == WEAPON_LASER || Weapon == WEAPON_NINJA;
}

bool IsFreezeTile(int Tile)
{
	return Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE;
}

bool SameServerAddress(const char *pAddress, const NETADDR &CurrentAddress)
{
	NETADDR ParsedAddress;
	if(net_addr_from_str(&ParsedAddress, pAddress) == 0)
		return net_addr_comp(&ParsedAddress, &CurrentAddress) == 0;

	char aCurrentAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(&CurrentAddress, aCurrentAddress, sizeof(aCurrentAddress), true);
	return str_comp_nocase(pAddress, aCurrentAddress) == 0;
}

float MarginFromPing(float Ping)
{
	return std::max((float)AUTO_MARGIN_BASE, Ping * 0.25f);
}
} // namespace

void CLazyClient::OnConsoleInit()
{
	Console()->Register("+lc_gun", "", CFGFLAG_CLIENT, ConGunFire, this, "Fire the gun in gores mode without using the hammer");
	Console()->Register("+lc_zerograv_forced", "", CFGFLAG_CLIENT, ConZeroGravForced, this, "Toggle forced zero-gravity freeze prediction");
}

void CLazyClient::OnReset()
{
	ResetSnapTap(0);
	ResetSnapTap(1);
	m_GoresHeavyWeapon = false;
	m_GoresAttacking = false;
	m_GoresFireHeld = false;
	m_GoresInjectedFire = false;
	m_GoresAttackStage = 0;
	m_GoresAttackEnd = 0;
	m_GoresWeaponSelectUntil = 0;
	m_GoresForcedWeapon = 0;
	m_GunKeyHeld = false;
	m_AntipingSmoothedPing = -1.0f;
	std::fill(std::begin(m_aClientTypes), std::end(m_aClientTypes), LC_CLIENT_NONE);
	m_ClientLogosInitialized = false;
	m_WasClientLogosEnabled = false;
	ResetBestClientList();
	DeactivateForcedZeroGravity();
	m_ZeroGravWarning = false;
	m_ZeroGravCheckTimer = 0.0f;
	m_ZeroGravHistoryCount = 0;
	m_ZeroGravHistoryNext = 0;
	m_ZeroGravTarget = -1;
	m_ZeroGravMissedChecks = 0;
	m_ZeroGravWarningHideTimer = 0.0f;
	m_ZeroGravFastChecks = 0;
	m_ZeroGravSpeedrunCooldown = 0;
	m_ZeroGravHasHookPos = false;
	ResetAutoMarginState();
	ResetTelemetry();
}

void CLazyClient::OnStateChange(int NewState, int OldState)
{
	if(NewState == OldState)
		return;
	ResetAutoMarginState();
	m_GoresHeavyWeapon = false;
	m_GoresAttacking = false;
	m_GoresFireHeld = false;
	m_GoresInjectedFire = false;
	m_GoresAttackStage = 0;
	m_GoresAttackEnd = 0;
	m_GoresWeaponSelectUntil = 0;
	m_GoresForcedWeapon = 0;
	m_GunKeyHeld = false;
	m_AntipingSmoothedPing = -1.0f;
	std::fill(std::begin(m_aClientTypes), std::end(m_aClientTypes), LC_CLIENT_NONE);
	ResetBestClientList();
	DeactivateForcedZeroGravity();
	m_ZeroGravWarning = false;
	m_ZeroGravCheckTimer = 0.0f;
	m_ZeroGravHistoryCount = 0;
	m_ZeroGravHistoryNext = 0;
	m_ZeroGravTarget = -1;
	m_ZeroGravMissedChecks = 0;
	m_ZeroGravWarningHideTimer = 0.0f;
	m_ZeroGravFastChecks = 0;
	m_ZeroGravSpeedrunCooldown = 0;
	m_ZeroGravHasHookPos = false;
	ResetTelemetry();
}

void CLazyClient::ApplyClientTypeToColor(int &ColorBody, int &ColorFeet) const
{
	if(!g_Config.m_LcClientLogos)
		return;
	ColorBody = LazyCCIDInsert(ColorBody, LC_CCID_BODY_LAZYCLIENT);
	ColorFeet = LazyCCIDInsert(ColorFeet, LC_CCID_FEET_LAZYCLIENT);
}

int CLazyClient::ClientType(int ClientId) const
{
	if(!g_Config.m_LcClientLogos || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return LC_CLIENT_NONE;
	return m_aClientTypes[ClientId];
}

IGraphics::CTextureHandle CLazyClient::ClientIcon(int Type) const
{
	switch(Type)
	{
	case LC_CLIENT_LAZYCLIENT:
		return g_pData->m_aImages[IMAGE_LAZYCLIENT].m_Id;
	case LC_CLIENT_RUSHIE:
		return g_pData->m_aImages[IMAGE_RUSHIE].m_Id;
	case LC_CLIENT_BESTCLIENT:
		return g_pData->m_aImages[IMAGE_BESTCLIENT].m_Id;
	default:
		return IGraphics::CTextureHandle();
	}
}

void CLazyClient::RenderClientIcon(int Type, vec2 Pos, float Size, float Alpha) const
{
	const IGraphics::CTextureHandle Texture = ClientIcon(Type);
	if(!Texture.IsValid() || Size <= 0.0f)
		return;

	Graphics()->TextureSet(Texture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	const IGraphics::CQuadItem QuadItem(Pos.x, Pos.y, Size, Size);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CLazyClient::UpdateClientTypes()
{
	std::fill(std::begin(m_aClientTypes), std::end(m_aClientTypes), LC_CLIENT_NONE);

	const int State = Client()->State();
	if((State != IClient::STATE_ONLINE && State != IClient::STATE_DEMOPLAYBACK) || !g_Config.m_LcClientLogos)
		return;

	const std::unordered_set<std::string> *pBestClientNames = nullptr;
	if(State == IClient::STATE_ONLINE && g_Config.m_LcBestClientPublicList && m_BestClientListLoaded)
	{
		const NETADDR CurrentAddress = Client()->ServerAddress();
		for(const auto &Entry : m_BestClientPlayers)
		{
			if(SameServerAddress(Entry.first.c_str(), CurrentAddress))
			{
				pBestClientNames = &Entry.second;
				break;
			}
		}
	}

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		const CGameClient::CClientData &Client = GameClient()->m_aClients[ClientId];
		int Type = LC_CLIENT_NONE;

		const int Body = LazyCCIDBody(Client.m_ColorBody);
		const int Feet = LazyCCIDFeet(Client.m_ColorFeet);
		if(Feet == LC_CCID_FEET_NONE)
		{
			switch(Body)
			{
			case LC_CCID_BODY_KAIZO:
				Type = LC_CLIENT_KAIZO;
				break;
			case LC_CCID_BODY_DUCK:
				Type = LC_CLIENT_DUCK;
				break;
			case LC_CCID_BODY_CHILLERBOT:
				Type = LC_CLIENT_CHILLERBOT;
				break;
			case LC_CCID_BODY_RUSHIE:
				Type = LC_CLIENT_RUSHIE;
				break;
			case LC_CCID_BODY_ASCEND:
				Type = LC_CLIENT_ASCEND;
				break;
			case LC_CCID_BODY_LAZYCLIENT:
				Type = LC_CLIENT_LAZYCLIENT;
				break;
			}
		}

		if(Type == LC_CLIENT_NONE && pBestClientNames != nullptr && Client.m_aName[0] != '\0' &&
			pBestClientNames->find(Client.m_aName) != pBestClientNames->end())
		{
			Type = LC_CLIENT_BESTCLIENT;
		}

		if(State == IClient::STATE_ONLINE &&
			(ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1]))
		{
			// The server may not have echoed our custom color yet, and 0.7
			// servers do not use the skin color convention.
			Type = LC_CLIENT_LAZYCLIENT;
		}

		m_aClientTypes[ClientId] = Type;
	}
}

void CLazyClient::UpdateClientLogoState()
{
	const bool Enabled = g_Config.m_LcClientLogos != 0;
	if(!m_ClientLogosInitialized)
	{
		m_ClientLogosInitialized = true;
		m_WasClientLogosEnabled = Enabled;
		return;
	}
	if(Enabled == m_WasClientLogosEnabled)
		return;

	m_WasClientLogosEnabled = Enabled;
	if(Client()->State() == IClient::STATE_ONLINE)
	{
		GameClient()->SendInfo(false);
		if(Client()->DummyConnected())
			GameClient()->SendDummyInfo(false);
	}
}

void CLazyClient::ResetBestClientList()
{
	if(m_pBestClientListTask)
	{
		m_pBestClientListTask->Abort();
		m_pBestClientListTask = nullptr;
	}
	m_BestClientListRefreshTimer = 0.0f;
	m_BestClientListLoaded = false;
	m_BestClientPlayers.clear();
}

void CLazyClient::FinishBestClientList()
{
	if(!m_pBestClientListTask || m_pBestClientListTask->State() != EHttpState::DONE)
		return;

	json_value *pJson = m_pBestClientListTask->ResultJson();
	if(pJson == nullptr || pJson->type != json_array)
	{
		if(pJson != nullptr)
			json_value_free(pJson);
		log_debug("lazy", "BestClient list has an unexpected format");
		return;
	}

	std::unordered_map<std::string, std::unordered_set<std::string>> NewPlayers;
	const int NumServers = json_array_length(pJson);
	for(int ServerIndex = 0; ServerIndex < NumServers; ++ServerIndex)
	{
		const json_value *pServer = json_array_get(pJson, ServerIndex);
		if(pServer == nullptr || pServer->type != json_object)
			continue;

		const json_value *pAddress = json_object_get(pServer, "server_address");
		const json_value *pPlayers = json_object_get(pServer, "players");
		if(pAddress == nullptr || pAddress->type != json_string || pPlayers == nullptr || pPlayers->type != json_array)
			continue;

		std::unordered_set<std::string> Names;
		const int NumPlayers = json_array_length(pPlayers);
		for(int PlayerIndex = 0; PlayerIndex < NumPlayers; ++PlayerIndex)
		{
			const json_value *pPlayer = json_array_get(pPlayers, PlayerIndex);
			if(pPlayer == nullptr || pPlayer->type != json_object)
				continue;

			const json_value *pName = json_object_get(pPlayer, "name");
			if(pName == nullptr || pName->type != json_string || pName->u.string.length == 0)
				continue;

			Names.emplace(pName->u.string.ptr, pName->u.string.length);
		}

		auto Entry = NewPlayers.try_emplace(std::string(pAddress->u.string.ptr, pAddress->u.string.length));
		Entry.first->second.insert(Names.begin(), Names.end());
	}
	json_value_free(pJson);

	m_BestClientPlayers = std::move(NewPlayers);
	m_BestClientListLoaded = true;
	log_debug("lazy", "loaded %d servers from the BestClient public list", (int)m_BestClientPlayers.size());
	UpdateClientTypes();
}

void CLazyClient::UpdateBestClientList()
{
	const bool Wanted = g_Config.m_LcClientLogos && g_Config.m_LcBestClientPublicList;
	if(!Wanted)
	{
		if(m_pBestClientListTask)
		{
			m_pBestClientListTask->Abort();
			m_pBestClientListTask = nullptr;
		}
		if(m_BestClientListLoaded || !m_BestClientPlayers.empty())
		{
			m_BestClientListLoaded = false;
			m_BestClientPlayers.clear();
			UpdateClientTypes();
		}
		m_BestClientListRefreshTimer = 0.0f;
		return;
	}

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		if(m_pBestClientListTask)
		{
			m_pBestClientListTask->Abort();
			m_pBestClientListTask = nullptr;
		}
		return;
	}

	if(m_pBestClientListTask)
	{
		if(m_pBestClientListTask->State() == EHttpState::DONE)
		{
			FinishBestClientList();
			m_pBestClientListTask = nullptr;
		}
		else if(m_pBestClientListTask->Done())
		{
			log_debug("lazy", "BestClient public list request failed");
			m_pBestClientListTask = nullptr;
			m_BestClientListRefreshTimer = BESTCLIENT_LIST_RETRY_SECONDS;
		}
		else
		{
			return;
		}
	}

	m_BestClientListRefreshTimer -= Client()->RenderFrameTime();
	if(m_BestClientListRefreshTimer > 0.0f)
		return;
	m_BestClientListRefreshTimer = BESTCLIENT_LIST_REFRESH_SECONDS;

	m_pBestClientListTask = HttpGet(BESTCLIENT_USERS_URL);
	if(!m_pBestClientListTask)
		return;
	m_pBestClientListTask->Timeout(CTimeout{10000, 15000, 500, 10});
	m_pBestClientListTask->MaxResponseSize(BESTCLIENT_LIST_MAX_RESPONSE_SIZE);
	m_pBestClientListTask->IpResolve(IPRESOLVE::V4);
	m_pBestClientListTask->VerifyPeer(false);
	Http()->Run(m_pBestClientListTask);
}

float CLazyClient::InputOffsetTicks() const
{
	switch(g_Config.m_LcInputMode)
	{
	case 1: // fast
		return g_Config.m_LcFastInputAmount / 20.0f;
	case 2: // best
	{
		float Offset = g_Config.m_LcBestInputAmount / 100.0f;
		if(g_Config.m_LcBestInputSmoothing > 0)
			Offset *= 1.0f - g_Config.m_LcBestInputSmoothing / 200.0f;
		if(g_Config.m_LcBestInputLatencyComp > 0)
			Offset *= 1.0f + g_Config.m_LcBestInputLatencyComp / 100.0f;
		return Offset;
	}
	case 3: // saiko
		return g_Config.m_LcSaikoInputAmount / 100.0f;
	case 4: // cloud
		return g_Config.m_LcCloudInputAmount / 100.0f;
	default:
		return g_Config.m_TcFastInput ? g_Config.m_TcFastInputAmount / 20.0f : 0.0f;
	}
}

bool CLazyClient::ApplyInputToOthers() const
{
	return g_Config.m_LcInputMode != 0 ? g_Config.m_LcInputOthers != 0 : g_Config.m_TcFastInputOthers != 0;
}

bool CLazyClient::InputModeActive() const
{
	return g_Config.m_LcInputMode != 0 || g_Config.m_TcFastInput != 0;
}

void CLazyClient::ResetSnapTap(int Dummy)
{
	m_aSnapApplied[Dummy] = 0;
	m_aSnapLastPressed[Dummy] = 0;
	m_aSnapLastPressedTime[Dummy] = 0;
}

void CLazyClient::OnMovementKey(bool Left, bool Pressed)
{
	if(!Pressed || !g_Config.m_LcSnapTap)
		return;

	const int Dummy = g_Config.m_ClDummy;
	m_aSnapLastPressed[Dummy] = Left ? -1 : 1;
	m_aSnapLastPressedTime[Dummy] = time_get();
}

int CLazyClient::ResolveMovementDirection(int Dummy, bool LeftPressed, bool RightPressed)
{
	if(LeftPressed == RightPressed)
	{
		if(!LeftPressed)
		{
			m_aSnapApplied[Dummy] = 0;
			return 0;
		}

		if(!g_Config.m_LcSnapTap)
			return 0;

		int CandidateDirection = m_aSnapLastPressed[Dummy];
		if(CandidateDirection != -1 && CandidateDirection != 1)
			CandidateDirection = m_aSnapApplied[Dummy] != 0 ? m_aSnapApplied[Dummy] : -1;

		if(m_aSnapApplied[Dummy] == 0)
		{
			m_aSnapApplied[Dummy] = CandidateDirection;
		}
		else if(m_aSnapApplied[Dummy] != CandidateDirection)
		{
			const int64_t Delay = (time_freq() * (int64_t)g_Config.m_LcSnapTapDelay) / 1000;
			if(Delay <= 0 || time_get() - m_aSnapLastPressedTime[Dummy] >= Delay)
				m_aSnapApplied[Dummy] = CandidateDirection;
		}

		return m_aSnapApplied[Dummy];
	}

	m_aSnapApplied[Dummy] = LeftPressed ? -1 : 1;
	return m_aSnapApplied[Dummy];
}

void CLazyClient::ConGunFire(IConsole::IResult *pResult, void *pUserData)
{
	CLazyClient *pThis = static_cast<CLazyClient *>(pUserData);
	const bool Pressed = pResult->GetInteger(0) != 0;
	pThis->m_GunKeyHeld = Pressed;
	if(Pressed)
	{
		pThis->m_GoresAttacking = false;
		pThis->m_GoresInjectedFire = false;
		pThis->m_GoresAttackStage = 0;
	}

	CControls &Controls = pThis->GameClient()->m_Controls;
	int *pFire = &Controls.m_aInputData[g_Config.m_ClDummy].m_Fire;
	if(((*pFire) & 1) != (Pressed ? 1 : 0))
		(*pFire)++;
	*pFire &= INPUT_STATE_MASK;
}

void CLazyClient::BindGunKey()
{
	BindGunKeyForced();
}

void CLazyClient::BindGunKeyForced()
{
	if(g_Config.m_LcGoresGunKey[0] == '\0')
		return;

	const int Key = Input()->FindKeyByName(g_Config.m_LcGoresGunKey);
	if(Key == KEY_UNKNOWN)
		return;

	if(m_GoresKeyBound && m_GoresBoundKey == Key)
		return;
	if(m_GoresKeyBound)
		RestoreGunKey();

	const char *pCurrentBind = GameClient()->m_Binds.Get(Key, KeyModifier::NONE);
	if(str_comp(pCurrentBind, "+lc_gun") == 0)
	{
		// Still bound from a previous session; recover the original bind from the config.
		str_copy(m_aGoresPrevBind, g_Config.m_LcGoresGunKeyPrev, sizeof(m_aGoresPrevBind));
	}
	else
	{
		str_copy(m_aGoresPrevBind, pCurrentBind, sizeof(m_aGoresPrevBind));
		str_copy(g_Config.m_LcGoresGunKeyPrev, pCurrentBind, sizeof(g_Config.m_LcGoresGunKeyPrev));
		GameClient()->m_Binds.Bind(Key, "+lc_gun");
	}
	m_GoresBoundKey = Key;
	m_GoresKeyBound = true;
}

void CLazyClient::RestoreGunKey()
{
	if(!m_GoresKeyBound)
		return;
	GameClient()->m_Binds.Bind(m_GoresBoundKey, m_aGoresPrevBind);
	g_Config.m_LcGoresGunKeyPrev[0] = '\0';
	m_aGoresPrevBind[0] = '\0';
	m_GoresKeyBound = false;
	m_GoresBoundKey = 0;
}

void CLazyClient::RestoreStaleGunKeyBind()
{
	if(g_Config.m_LcGoresGunKeyPrev[0] == '\0')
		return;

	const int Key = Input()->FindKeyByName(g_Config.m_LcGoresGunKey);
	if(Key == KEY_UNKNOWN)
	{
		g_Config.m_LcGoresGunKeyPrev[0] = '\0';
		return;
	}

	if(str_comp(GameClient()->m_Binds.Get(Key, KeyModifier::NONE), "+lc_gun") == 0)
		GameClient()->m_Binds.Bind(Key, g_Config.m_LcGoresGunKeyPrev);
	g_Config.m_LcGoresGunKeyPrev[0] = '\0';
}

void CLazyClient::BindZeroGravKey()
{
	BindZeroGravKeyForced();
}

void CLazyClient::BindZeroGravKeyForced()
{
	if(g_Config.m_LcZeroGravForcedKey[0] == '\0')
	{
		if(m_ZeroGravKeyBound)
			RestoreZeroGravKey();
		return;
	}

	const int Key = Input()->FindKeyByName(g_Config.m_LcZeroGravForcedKey);
	if(Key == KEY_UNKNOWN)
		return;

	if(m_ZeroGravKeyBound && m_ZeroGravBoundKey == Key)
		return;
	if(m_ZeroGravKeyBound)
		RestoreZeroGravKey();

	const char *pCurrentBind = GameClient()->m_Binds.Get(Key, KeyModifier::NONE);
	if(str_comp(pCurrentBind, "+lc_zerograv_forced") == 0)
	{
		// Still bound from a previous session; recover the original bind from the config.
		str_copy(m_aZeroGravPrevBind, g_Config.m_LcZeroGravForcedKeyPrev, sizeof(m_aZeroGravPrevBind));
	}
	else
	{
		str_copy(m_aZeroGravPrevBind, pCurrentBind, sizeof(m_aZeroGravPrevBind));
		str_copy(g_Config.m_LcZeroGravForcedKeyPrev, pCurrentBind, sizeof(g_Config.m_LcZeroGravForcedKeyPrev));
		GameClient()->m_Binds.Bind(Key, "+lc_zerograv_forced");
	}
	m_ZeroGravBoundKey = Key;
	m_ZeroGravKeyBound = true;
}

void CLazyClient::RestoreZeroGravKey()
{
	if(!m_ZeroGravKeyBound)
		return;
	GameClient()->m_Binds.Bind(m_ZeroGravBoundKey, m_aZeroGravPrevBind);
	g_Config.m_LcZeroGravForcedKeyPrev[0] = '\0';
	m_aZeroGravPrevBind[0] = '\0';
	m_ZeroGravKeyBound = false;
	m_ZeroGravBoundKey = 0;
}

void CLazyClient::RestoreStaleZeroGravKeyBind()
{
	if(g_Config.m_LcZeroGravForcedKeyPrev[0] == '\0')
		return;

	const int Key = Input()->FindKeyByName(g_Config.m_LcZeroGravForcedKey);
	if(Key == KEY_UNKNOWN)
	{
		g_Config.m_LcZeroGravForcedKeyPrev[0] = '\0';
		return;
	}

	if(str_comp(GameClient()->m_Binds.Get(Key, KeyModifier::NONE), "+lc_zerograv_forced") == 0)
		GameClient()->m_Binds.Bind(Key, g_Config.m_LcZeroGravForcedKeyPrev);
	g_Config.m_LcZeroGravForcedKeyPrev[0] = '\0';
}

void CLazyClient::UpdateGoresMode()
{
	CControls &Controls = GameClient()->m_Controls;
	const int Dummy = g_Config.m_ClDummy;
	int *pWanted = &Controls.m_aInputData[Dummy].m_WantedWeapon;
	int *pFire = &Controls.m_aInputData[Dummy].m_Fire;

	if(!g_Config.m_LcGoresMode || !GameClient()->m_Snap.m_pLocalCharacter)
	{
		m_GoresHeavyWeapon = false;
		m_GoresAttacking = false;
		m_GoresFireHeld = false;
		m_GoresInjectedFire = false;
		m_GoresAttackStage = 0;
		m_GoresAttackEnd = 0;
		m_GoresForcedWeapon = 0;
		return;
	}

	// Respect any weapon the player selects, including pickups and
	// next/previous weapon binds. Gores mode pauses briefly so that the
	// selection is not immediately overwritten by the held gun.
	if(*pWanted != 0 && *pWanted != m_GoresForcedWeapon)
	{
		m_GoresForcedWeapon = *pWanted;
		m_GoresHeavyWeapon = IsHeavyWeapon(*pWanted - 1);
		m_GoresAttacking = false;
		m_GoresInjectedFire = false;
		m_GoresAttackStage = 0;
		m_GoresWeaponSelectUntil = time_get() + (time_freq() * GORES_WEAPON_SELECT_GRACE_MS) / 1000;
		return;
	}
	if(time_get() < m_GoresWeaponSelectUntil)
		return;

	// The dedicated gun key always forces the gun, even while a heavy
	// weapon is held and the ignore option is enabled.
	if(m_GunKeyHeld)
	{
		m_GoresAttacking = false;
		m_GoresInjectedFire = false;
		m_GoresAttackStage = 0;
		m_GoresAttackEnd = 0;
		m_GoresHeavyWeapon = false;
		*pWanted = WEAPON_GUN + 1;
		m_GoresForcedWeapon = *pWanted;
		return;
	}

	const int CurWeapon = GameClient()->m_PredictedChar.m_ActiveWeapon;
	m_GoresHeavyWeapon = IsHeavyWeapon(CurWeapon);
	if(!g_Config.m_LcGoresIgnoreHeavy && m_GoresHeavyWeapon)
	{
		m_GoresAttacking = false;
		m_GoresInjectedFire = false;
		m_GoresAttackStage = 0;
		m_GoresAttackEnd = 0;
		m_GoresForcedWeapon = 0;
		return;
	}

	// Do not block the player's fire input forever if the hammer never becomes
	// active (for example after a prediction mismatch or a frozen character).
	if((m_GoresAttackStage == 1 || m_GoresAttackStage == 2) && m_GoresAttackEnd > 0 && time_get() >= m_GoresAttackEnd)
	{
		m_GoresAttacking = false;
		m_GoresInjectedFire = false;
		m_GoresAttackStage = 0;
		m_GoresAttackEnd = 0;
		*pWanted = WEAPON_GUN + 1;
		m_GoresForcedWeapon = *pWanted;
		Controls.m_ForceSend = true;
		return;
	}

	if(m_GoresAttackStage > 0)
	{
		if(m_GoresAttackStage == 4)
		{
			// Release the synthesized click if the player already let go.
			if(m_GoresInjectedFire && !m_GoresFireHeld && (*pFire & 1) != 0)
			{
				(*pFire)++;
				*pFire &= INPUT_STATE_MASK;
				m_GoresInjectedFire = false;
			}

			if(time_get() >= m_GoresAttackEnd)
			{
				// Never carry the synthesized press into the gun.
				if((*pFire & 1) != 0)
				{
					(*pFire)++;
					*pFire &= INPUT_STATE_MASK;
				}
				m_GoresInjectedFire = false;
				m_GoresAttackStage = 0;
				m_GoresAttacking = false;
				*pWanted = WEAPON_GUN + 1;
				m_GoresForcedWeapon = *pWanted;
				Controls.m_ForceSend = true;
				return;
			}
		}
		else if(m_GoresAttackStage == 3)
		{
			// The hammer is selected; fire one tick later so the hammer is
			// visible before the hit and the server has processed the switch.
			if((*pFire & 1) == 0)
			{
				(*pFire)++;
				*pFire &= INPUT_STATE_MASK;
			}
			m_GoresInjectedFire = true;
			m_GoresAttackEnd = time_get() + (time_freq() * g_Config.m_LcGoresSwitchDelay) / 1000;
			m_GoresAttackStage = 4;
		}
		else if(m_GoresAttackStage == 2 && GameClient()->m_PredictedChar.m_ActiveWeapon == WEAPON_HAMMER)
		{
			// The hammer is active for at least one tick. Fire on the next
			// input to guarantee GUN -> HAMMER -> HIT -> GUN.
			m_GoresAttackStage = 3;
		}
		else
		{
			// Request the hammer and keep the input flowing until the switch
			// completes. This also waits out the gun's reload timer.
			m_GoresAttackStage = 2;
		}

		*pWanted = WEAPON_HAMMER + 1;
		m_GoresForcedWeapon = *pWanted;
		Controls.m_ForceSend = true;
		return;
	}

	*pWanted = WEAPON_GUN + 1;
	m_GoresForcedWeapon = *pWanted;
}

void CLazyClient::OnWeaponSelectInput()
{
	if(!g_Config.m_LcGoresMode)
		return;
	m_GoresAttacking = false;
	m_GoresInjectedFire = false;
	m_GoresAttackStage = 0;
	m_GoresAttackEnd = 0;
	m_GoresWeaponSelectUntil = time_get() + (time_freq() * GORES_WEAPON_SELECT_GRACE_MS) / 1000;
}

void CLazyClient::OnFireInput(int Pressed, bool Changed)
{
	if(!g_Config.m_LcGoresMode)
		return;

	// Track the physical button even while gores mode is paused, so a staged
	// attack can release its synthesized click correctly.
	m_GoresFireHeld = Pressed != 0;

	if(m_GunKeyHeld)
		return;
	if(time_get() < m_GoresWeaponSelectUntil)
		return;
	if(!g_Config.m_LcGoresIgnoreHeavy && m_GoresHeavyWeapon)
		return;

	if(!Changed)
	{
		// The press was suppressed, so its matching release does not change
		// the counter either. Still cancel a click that ended before the
		// hammer was requested.
		if(!Pressed && m_GoresAttackStage == 1)
		{
			m_GoresAttacking = false;
			m_GoresAttackStage = 0;
			m_GoresAttackEnd = 0;
		}
		return;
	}

	CControls &Controls = GameClient()->m_Controls;
	int *pFire = &Controls.m_aInputData[g_Config.m_ClDummy].m_Fire;

	if(Pressed)
	{
		m_GoresAttacking = true;
		m_GoresAttackStage = 1;
		m_GoresInjectedFire = false;
		// Deadline for selecting the hammer. Overwritten with the switch-back
		// delay once the hit is injected.
		m_GoresAttackEnd = time_get() + time_freq() / 2;

		// Do not let the gun fire before the hammer has been selected. The
		// counter was just incremented by the press, so undo that increment.
		// Moving to the next even value would still count as a press, because
		// CountInput counts every crossed odd state.
		if((*pFire & 1) != 0)
			(*pFire)--;
		*pFire &= INPUT_STATE_MASK;
		Controls.m_ForceSend = true;
	}
	else
	{
		// Cancel only if the hammer switch has not been requested yet.
		// A click that lasted at least one tick still completes.
		if(m_GoresAttackStage == 1)
		{
			m_GoresAttacking = false;
			m_GoresAttackStage = 0;
			m_GoresAttackEnd = 0;
		}
	}
}

void CLazyClient::ResetAutoMarginState()
{
	m_CheckTimer = 0.0f;
	m_LatestPing = -1.0f;
	m_IntervalPingSum = 0.0f;
	m_IntervalPingSamples = 0;
	m_IntervalMinPing = -1.0f;
	m_IntervalPeakPing = -1.0f;
	m_SmoothedPing = -1.0f;
	m_SmoothedJitter = 0.0f;
	m_HighPing = false;
}

void CLazyClient::OnNewSnapshot()
{
	UpdateClientTypes();

	if(g_Config.m_LcTelemetry && Client()->State() == IClient::STATE_ONLINE && GameClient()->m_Snap.m_pLocalInfo)
	{
		const float Ping = (float)std::max(0, GameClient()->m_Snap.m_pLocalInfo->m_Latency);
		if(m_TelemetryLastPing >= 0.0f)
		{
			const float Delta = std::abs(Ping - m_TelemetryLastPing);
			m_TelemetryJitter += (Delta - m_TelemetryJitter) * 0.05f;
		}
		m_TelemetryLastPing = Ping;
		m_TelemetryPing = Ping;
		m_TelemetrySnapshots++;
	}
	else
	{
		m_TelemetryLastPing = -1.0f;
	}

	if(Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalInfo)
	{
		ResetAutoMarginState();
		return;
	}

	const float CurrentPing = (float)std::max(0, GameClient()->m_Snap.m_pLocalInfo->m_Latency);
	m_LatestPing = CurrentPing;
	m_IntervalPingSum += CurrentPing;
	++m_IntervalPingSamples;
	if(m_IntervalMinPing < 0.0f || CurrentPing < m_IntervalMinPing)
		m_IntervalMinPing = CurrentPing;
	if(m_IntervalPeakPing < 0.0f || CurrentPing > m_IntervalPeakPing)
		m_IntervalPeakPing = CurrentPing;
}

void CLazyClient::UpdateAutoMargin()
{
	if(!g_Config.m_LcAutoMargin)
	{
		if(m_WasAutoMarginEnabled)
		{
			if(m_SavedMargin >= 0)
				g_Config.m_ClPredictionMargin = m_SavedMargin;
			m_WasAutoMarginEnabled = false;
			m_SavedMargin = -1;
		}
		ResetAutoMarginState();
		return;
	}

	if(!m_WasAutoMarginEnabled)
	{
		m_SavedMargin = g_Config.m_ClPredictionMargin;
		m_WasAutoMarginEnabled = true;
	}

	if(Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalInfo)
	{
		ResetAutoMarginState();
		return;
	}

	m_CheckTimer -= Client()->RenderFrameTime();
	if(m_CheckTimer > 0.0f)
		return;
	m_CheckTimer += AUTO_MARGIN_CHECK_INTERVAL;
	if(m_CheckTimer < 0.0f)
		m_CheckTimer = 0.0f;

	const float CurrentPing = m_LatestPing >= 0.0f ? m_LatestPing : (float)std::max(0, GameClient()->m_Snap.m_pLocalInfo->m_Latency);
	const float IntervalAveragePing = m_IntervalPingSamples > 0 ? m_IntervalPingSum / (float)m_IntervalPingSamples : CurrentPing;
	const float IntervalMinPing = m_IntervalMinPing >= 0.0f ? m_IntervalMinPing : CurrentPing;
	const float SamplePing = m_IntervalPeakPing >= 0.0f ? m_IntervalPeakPing : CurrentPing;
	m_IntervalPingSum = 0.0f;
	m_IntervalPingSamples = 0;
	m_IntervalMinPing = -1.0f;
	m_IntervalPeakPing = -1.0f;
	const float IntervalJitter = std::max(0.0f, SamplePing - IntervalMinPing);

	if(m_SmoothedPing < 0.0f)
		m_SmoothedPing = IntervalAveragePing;
	else
	{
		const float SmoothFactor = IntervalAveragePing > m_SmoothedPing ? AUTO_MARGIN_PING_SMOOTH_RISE : AUTO_MARGIN_PING_SMOOTH_FALL;
		m_SmoothedPing += (IntervalAveragePing - m_SmoothedPing) * SmoothFactor;
	}

	const float JitterSmoothFactor = IntervalJitter > m_SmoothedJitter ? AUTO_MARGIN_JITTER_SMOOTH_RISE : AUTO_MARGIN_JITTER_SMOOTH_FALL;
	m_SmoothedJitter += (IntervalJitter - m_SmoothedJitter) * JitterSmoothFactor;

	const float EffectivePing = std::max(std::max(m_SmoothedPing, CurrentPing), IntervalAveragePing + m_SmoothedJitter * 0.25f);
	const bool WantsDynamicMargin = EffectivePing >= (float)AUTO_MARGIN_HIGH_PING_ENTER || SamplePing >= (float)AUTO_MARGIN_HIGH_PING_ENTER || m_SmoothedJitter >= AUTO_MARGIN_JITTER_ENTER;

	if(m_HighPing)
	{
		if(EffectivePing < (float)AUTO_MARGIN_HIGH_PING_EXIT && m_SmoothedJitter <= AUTO_MARGIN_JITTER_EXIT)
			m_HighPing = false;
	}
	else if(WantsDynamicMargin)
	{
		m_HighPing = true;
	}

	int TargetMargin = AUTO_MARGIN_BASE;
	if(m_HighPing)
	{
		const float StableMargin = MarginFromPing(EffectivePing);
		const float JitterMargin = std::max(m_SmoothedJitter * 0.90f, IntervalJitter * 0.35f);
		TargetMargin = std::clamp((int)std::round(StableMargin + JitterMargin), AUTO_MARGIN_BASE, AUTO_MARGIN_MAX);
	}

	int CurrentMargin = std::clamp(g_Config.m_ClPredictionMargin, AUTO_MARGIN_BASE, AUTO_MARGIN_MAX);
	const int MarginDelta = TargetMargin - CurrentMargin;
	if(std::abs(MarginDelta) <= AUTO_MARGIN_DEADZONE)
		return;

	if(MarginDelta > 0)
		CurrentMargin = TargetMargin;
	else
		CurrentMargin -= std::min(-MarginDelta, AUTO_MARGIN_STEP_DOWN);

	CurrentMargin = std::clamp(CurrentMargin, AUTO_MARGIN_BASE, AUTO_MARGIN_MAX);
	if(g_Config.m_ClPredictionMargin != CurrentMargin)
		g_Config.m_ClPredictionMargin = CurrentMargin;
}

void CLazyClient::UpdateAntiping()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		m_AntipingSmoothedPing = -1.0f;
		return;
	}

	int Ping = -1;
	if(GameClient()->m_Snap.m_pLocalInfo)
		Ping = std::max(0, GameClient()->m_Snap.m_pLocalInfo->m_Latency);
	else if(m_LatestPing >= 0.0f)
		Ping = (int)m_LatestPing;

	if(Ping >= 0)
	{
		if(m_AntipingSmoothedPing < 0.0f)
			m_AntipingSmoothedPing = (float)Ping;
		else
			m_AntipingSmoothedPing += ((float)Ping - m_AntipingSmoothedPing) * 0.25f;
	}

	bool Enable;
	switch(g_Config.m_LcAntipingMode)
	{
	case 0:
		Enable = false;
		break;
	case 2:
		Enable = m_AntipingSmoothedPing < 0.0f || m_AntipingSmoothedPing >= (float)g_Config.m_LcAntipingAutoPing;
		break;
	default:
		Enable = true;
		break;
	}

	if(g_Config.m_ClAntiPing != (Enable ? 1 : 0))
		g_Config.m_ClAntiPing = Enable ? 1 : 0;
}

void CLazyClient::ApplyAspect(int Width, int Height)
{
	Graphics()->SetCustomAspect(Width > 0 && Height > 0 ? (float)Width / (float)Height : 0.0f);
}

void CLazyClient::UpdateAspectConfirm()
{
	const int Width = g_Config.m_LcAspectWidth;
	const int Height = g_Config.m_LcAspectHeight;
	const bool Active = Width > 0 && Height > 0;

	if(!m_AspectSeeded)
	{
		m_AspectSeeded = true;
		m_AspectLastWidth = Width;
		m_AspectLastHeight = Height;
		m_AspectPrevWidth = Width;
		m_AspectPrevHeight = Height;
		ApplyAspect(Width, Height);
		return;
	}

	if(Width != m_AspectLastWidth || Height != m_AspectLastHeight)
	{
		if(Active)
		{
			if(!m_AspectConfirm)
			{
				m_AspectPrevWidth = m_AspectLastWidth;
				m_AspectPrevHeight = m_AspectLastHeight;
			}
			m_AspectConfirm = true;
			m_AspectDeadline = time_get() + time_freq() * ASPECT_CONFIRM_SECONDS;
		}
		else
		{
			m_AspectConfirm = false;
			m_AspectPrevWidth = 0;
			m_AspectPrevHeight = 0;
		}
		m_AspectLastWidth = Width;
		m_AspectLastHeight = Height;
		ApplyAspect(Width, Height);
	}

	if(m_AspectConfirm && time_get() >= m_AspectDeadline)
	{
		RevertAspect();
		return;
	}

	// Keep the projection in sync. This also retries the initial apply if the
	// graphics backend was not ready yet on the first update.
	ApplyAspect(Width, Height);
}

int CLazyClient::AspectSecondsLeft() const
{
	if(!m_AspectConfirm)
		return 0;
	const int64_t Remaining = m_AspectDeadline - time_get();
	return std::max(0, (int)((Remaining + time_freq() - 1) / time_freq()));
}

void CLazyClient::KeepAspect()
{
	m_AspectConfirm = false;
	m_AspectPrevWidth = g_Config.m_LcAspectWidth;
	m_AspectPrevHeight = g_Config.m_LcAspectHeight;
	m_AspectLastWidth = m_AspectPrevWidth;
	m_AspectLastHeight = m_AspectPrevHeight;
}

void CLazyClient::RevertAspect()
{
	g_Config.m_LcAspectWidth = m_AspectPrevWidth;
	g_Config.m_LcAspectHeight = m_AspectPrevHeight;
	m_AspectLastWidth = m_AspectPrevWidth;
	m_AspectLastHeight = m_AspectPrevHeight;
	m_AspectConfirm = false;
	ApplyAspect(m_AspectPrevWidth, m_AspectPrevHeight);
}

bool CLazyClient::PathHitsFreeze(CCollision *pCollision, vec2 PrevPos, vec2 Pos) const
{
	if(!pCollision)
		return false;

	const std::vector<int> vIndices = pCollision->GetMapIndices(PrevPos, Pos);
	for(const int Index : vIndices)
	{
		if(!pCollision->TileExists(Index))
			continue;
		if(IsFreezeTile(pCollision->GetTileIndex(Index)) || IsFreezeTile(pCollision->GetFrontTileIndex(Index)))
			return true;
	}
	return false;
}

int CLazyClient::FindZeroGravityTarget() const
{
	const CGameClient::CSnapState &Snap = GameClient()->m_Snap;
	if(!Snap.m_SpecInfo.m_Active)
		return Snap.m_LocalClientId;
	if(Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
		return Snap.m_SpecInfo.m_SpectatorId;

	// In freeview, predict for the visible tee closest to the camera center.
	const vec2 Center = GameClient()->m_Camera.m_Center;
	float VisibleWidth, VisibleHeight;
	Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, &VisibleWidth, &VisibleHeight);
	const float Margin = 64.0f;
	int BestId = -1;
	float BestDistance = 0.0f;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!Snap.m_aCharacters[ClientId].m_Active)
			continue;
		const CNetObj_Character &Character = Snap.m_aCharacters[ClientId].m_Cur;
		const vec2 Pos(Character.m_X, Character.m_Y);
		if(std::abs(Pos.x - Center.x) > VisibleWidth / 2.0f + Margin ||
			std::abs(Pos.y - Center.y) > VisibleHeight / 2.0f + Margin)
			continue;
		const float Distance = distance(Pos, Center);
		if(BestId == -1 || Distance < BestDistance)
		{
			BestId = ClientId;
			BestDistance = Distance;
		}
	}
	return BestId;
}

void CLazyClient::ClearZeroGravityWarning(bool Immediate)
{
	if(Immediate || !m_ZeroGravWarning)
	{
		m_ZeroGravWarning = false;
		m_ZeroGravWarningHideTimer = 0.0f;
		return;
	}
	if(m_ZeroGravWarningHideTimer <= 0.0f)
		m_ZeroGravWarningHideTimer = ZERO_GRAV_WARNING_HIDE_DELAY;
}

void CLazyClient::UpdateZeroGravityWarningHideTimer()
{
	if(m_ZeroGravWarningHideTimer <= 0.0f)
		return;
	m_ZeroGravWarningHideTimer -= Client()->RenderFrameTime();
	if(m_ZeroGravWarningHideTimer <= 0.0f)
	{
		m_ZeroGravWarningHideTimer = 0.0f;
		m_ZeroGravWarning = false;
	}
}

void CLazyClient::ConZeroGravForced(IConsole::IResult *pResult, void *pUserData)
{
	CLazyClient *pThis = static_cast<CLazyClient *>(pUserData);
	if(pResult->GetInteger(0) == 0)
	{
		pThis->m_ZeroGravForcedBindHeld = false;
		return;
	}
	// The engine repeats active binds while a key is held. Only the initial
	// press may toggle forced mode.
	if(pThis->m_ZeroGravForcedBindHeld)
		return;
	pThis->m_ZeroGravForcedBindHeld = true;
	pThis->ToggleForcedZeroGravity();
}

void CLazyClient::ToggleForcedZeroGravity()
{
	if(g_Config.m_LcZeroGravMode != 1 || !g_Config.m_LcZeroGravPredict)
		return;
	if(GameClient()->m_Chat.IsActive() || GameClient()->m_Menus.IsActive() || GameClient()->m_GameConsole.IsActive())
		return;

	if(m_ZeroGravForcedState != ZERO_GRAV_FORCED_OFF)
	{
		DeactivateForcedZeroGravity();
		return;
	}
	ActivateForcedZeroGravity();
}

bool CLazyClient::BuildForcedZeroGravityState(CCharacterCore &Core, CNetObj_PlayerInput &Input, bool &SnapshotHooked, vec2 &HookPos) const
{
	const CGameClient::CSnapState &Snap = GameClient()->m_Snap;
	if(Client()->State() != IClient::STATE_ONLINE || Snap.m_SpecInfo.m_Active || !Snap.m_pLocalCharacter)
		return false;

	Core = GameClient()->m_PredictedChar;
	Input = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
	SnapshotHooked = Snap.m_pLocalCharacter->m_HookState == HOOK_GRABBED;
	if(Core.m_HookState == HOOK_GRABBED)
		HookPos = Core.m_HookPos;
	else
		HookPos = vec2(Snap.m_pLocalCharacter->m_HookX, Snap.m_pLocalCharacter->m_HookY);
	return true;
}

bool CLazyClient::IsForcedZeroGravityHold(const CCharacterCore &Core, const CNetObj_PlayerInput &Input, bool SnapshotHooked, vec2 HookPos) const
{
	if((Core.m_HookState != HOOK_GRABBED && !SnapshotHooked) ||
		Core.HookedPlayer() != -1 ||
		Core.m_FreezeEnd > 0 || Core.m_DeepFrozen || Core.m_LiveFrozen || Core.m_IsInFreeze ||
		Core.m_Super || Core.m_Invincible)
		return false;

	const vec2 HookDelta = HookPos - Core.m_Pos;
	const int HoldDirection = Input.m_Direction;
	return Core.HookedPlayer() == -1 &&
	       HoldDirection != 0 &&
	       HookDelta.x * HoldDirection < 0.0f &&
	       std::abs(HookDelta.x) > 8.0f && length(HookDelta) > 46.0f &&
	       !Collision()->IsOnGround(Core.m_Pos, CCharacterCore::PhysicalSize());
}

float CLazyClient::SimulateForcedZeroGravity(const CCharacterCore &Core, const CNetObj_PlayerInput &Input, bool SnapshotHooked, vec2 HookPos) const
{
	CCharacterCore Sim = Core;
	Sim.SetCoreWorld(nullptr, Collision(), nullptr);
	// Forced mode only activates while the tee is already hooked. Keep the hook
	// attached even if the bind key replaced the hook key or the user released
	// it to reach the bind; the saved hold is what is being predicted.
	Sim.m_Input.m_Hook = 1;
	Sim.m_Input.m_Direction = Input.m_Direction;
	Sim.m_Input.m_Jump = Input.m_Jump;
	if(Sim.m_HookState != HOOK_GRABBED && SnapshotHooked)
	{
		Sim.m_HookState = HOOK_GRABBED;
		Sim.m_HookPos = HookPos;
		Sim.m_HookTick = 0;
		Sim.SetHookedPlayer(-1);
	}

	const int TickSpeed = std::max(1, Client()->GameTickSpeed());
	const int MaxTicks = std::max(1, g_Config.m_LcZeroGravPredictTime) * TickSpeed;
	vec2 PrevPos = Sim.m_Pos;
	for(int Tick = 1; Tick <= MaxTicks; Tick++)
	{
		Sim.Tick(true, false);
		Sim.Move();
		Sim.Quantize();
		if(PathHitsFreeze(Collision(), PrevPos, Sim.m_Pos))
			return Tick / (float)TickSpeed;
		PrevPos = Sim.m_Pos;
	}
	return -1.0f;
}

void CLazyClient::CaptureForcedZeroGravityCandidate()
{
	CCharacterCore Core;
	CNetObj_PlayerInput Input;
	bool SnapshotHooked = false;
	vec2 HookPos(0.0f, 0.0f);
	if(!BuildForcedZeroGravityState(Core, Input, SnapshotHooked, HookPos) ||
		!IsForcedZeroGravityHold(Core, Input, SnapshotHooked, HookPos))
		return;

	// Keep the last hold that actually freezes within the warning horizon. The
	// exact tick where the bind is pressed can be a safe phase of the sway, but
	// the user wants the warning for the dangerous phase they are holding.
	const float Seconds = SimulateForcedZeroGravity(Core, Input, SnapshotHooked, HookPos);
	if(Seconds < 0.0f)
		return;

	m_ZeroGravForcedCandidateCore = Core;
	m_ZeroGravForcedCandidateInput = Input;
	m_ZeroGravForcedCandidateSnapshotHooked = SnapshotHooked;
	m_ZeroGravForcedCandidateHookPos = HookPos;
	m_ZeroGravForcedCandidateSeconds = Seconds;
	m_ZeroGravForcedCandidateTime = Client()->LocalTime();
	m_ZeroGravForcedCandidateValid = true;
}

void CLazyClient::ActivateForcedZeroGravity()
{
	CCharacterCore Core;
	CNetObj_PlayerInput Input;
	bool SnapshotHooked = false;
	vec2 HookPos(0.0f, 0.0f);
	if(!BuildForcedZeroGravityState(Core, Input, SnapshotHooked, HookPos))
		return;

	const bool Valid = IsForcedZeroGravityHold(Core, Input, SnapshotHooked, HookPos);
	float Seconds = Valid ? SimulateForcedZeroGravity(Core, Input, SnapshotHooked, HookPos) : -1.0f;
	const bool CurrentlyHooked = Core.m_HookState == HOOK_GRABBED || SnapshotHooked;
	const bool CurrentStatusValid = Core.HookedPlayer() == -1 &&
					!(Core.m_FreezeEnd > 0 || Core.m_DeepFrozen || Core.m_LiveFrozen || Core.m_IsInFreeze ||
						Core.m_Super || Core.m_Invincible) &&
					!Collision()->IsOnGround(Core.m_Pos, CCharacterCore::PhysicalSize());
	const float Now = Client()->LocalTime();
	if(Seconds < 0.0f && CurrentlyHooked && CurrentStatusValid &&
		m_ZeroGravForcedCandidateValid && Now - m_ZeroGravForcedCandidateTime <= ZERO_GRAV_FORCED_CANDIDATE_AGE)
	{
		// The bind can be pressed during a safe sway phase, after releasing the
		// keys to reach it, or on a key that replaces the hook/direction bind.
		// Use the most recent hold that was actually dangerous.
		Core = m_ZeroGravForcedCandidateCore;
		Input = m_ZeroGravForcedCandidateInput;
		SnapshotHooked = m_ZeroGravForcedCandidateSnapshotHooked;
		HookPos = m_ZeroGravForcedCandidateHookPos;
		Seconds = m_ZeroGravForcedCandidateSeconds;
	}

	m_ZeroGravForcedCore = Core;
	m_ZeroGravForcedInput = Input;
	m_ZeroGravForcedSnapshotHooked = SnapshotHooked;
	m_ZeroGravForcedHookPos = HookPos;
	m_ZeroGravForcedSeconds = Seconds;

	m_ZeroGravForcedState = ZERO_GRAV_FORCED_WAIT_SECOND;
	m_ZeroGravForcedStartTime = Client()->LocalTime();
	ResetForcedZeroGravityInputState();
	m_ZeroGravTarget = GameClient()->m_Snap.m_LocalClientId;
	m_ZeroGravWarning = false;
	m_ZeroGravWarningHideTimer = 0.0f;
	m_ZeroGravSeconds = 0.0f;
	Console()->ExecuteLine("say /spec", IConsole::CLIENT_ID_UNSPECIFIED);
}

void CLazyClient::DeactivateForcedZeroGravity()
{
	m_ZeroGravForcedState = ZERO_GRAV_FORCED_OFF;
	m_ZeroGravForcedStartTime = 0.0f;
	m_ZeroGravForcedInputGraceUntil = 0.0f;
	std::fill(std::begin(m_aZeroGravForcedKeysDown), std::end(m_aZeroGravForcedKeysDown), false);
	m_ZeroGravForcedSeconds = -1.0f;
	m_ZeroGravForcedSnapshotHooked = false;
	m_ZeroGravForcedHookPos = vec2(0.0f, 0.0f);
	m_ZeroGravForcedCore.Reset();
	m_ZeroGravForcedInput = {};
	m_ZeroGravForcedCandidateValid = false;
	m_ZeroGravForcedCandidateTime = -1.0f;
	m_ZeroGravForcedCandidateCheck = 0.0f;
	m_ZeroGravForcedCandidateSeconds = -1.0f;
	m_ZeroGravForcedCandidateCore.Reset();
	m_ZeroGravForcedCandidateInput = {};
	m_ZeroGravForcedCandidateSnapshotHooked = false;
	m_ZeroGravForcedCandidateHookPos = vec2(0.0f, 0.0f);
	m_ZeroGravWarning = false;
	m_ZeroGravWarningHideTimer = 0.0f;
	m_ZeroGravSeconds = 0.0f;
	m_ZeroGravTarget = -1;
}

void CLazyClient::ResetForcedZeroGravityInputState()
{
	for(int Key = KEY_FIRST; Key < KEY_LAST; Key++)
		m_aZeroGravForcedKeysDown[Key] = Input()->KeyIsPressed(Key);
}

bool CLazyClient::AnyForcedZeroGravityBindKeyPressed() const
{
	for(int Key = KEY_FIRST; Key < KEY_LAST; Key++)
	{
		const char *pBind = GameClient()->m_Binds.Get(Key, KeyModifier::NONE);
		if(pBind && str_find(pBind, "+lc_zerograv_forced") && Input()->KeyIsPressed(Key))
			return true;
	}
	return false;
}

bool CLazyClient::ForcedZeroGravityInputPressed()
{
	const bool IgnoreKeyboard = GameClient()->m_Chat.IsActive() || GameClient()->m_Menus.IsActive() || GameClient()->m_GameConsole.IsActive();
	const bool Scoreboard = GameClient()->m_Scoreboard.IsActive();
	bool Pressed = false;
	for(int Key = KEY_FIRST; Key < KEY_LAST; Key++)
	{
		const bool Down = Input()->KeyIsPressed(Key);
		const bool WasDown = m_aZeroGravForcedKeysDown[Key];
		m_aZeroGravForcedKeysDown[Key] = Down;
		if(!Down || WasDown || IgnoreKeyboard)
			continue;
		if(Scoreboard && Key >= KEY_MOUSE_1 && Key <= KEY_MOUSE_WHEEL_RIGHT)
			continue;
		const char *pBind = GameClient()->m_Binds.Get(Key, KeyModifier::NONE);
		if(pBind && str_find(pBind, "+lc_zerograv_forced"))
			continue;
		Pressed = true;
	}
	return Pressed;
}

void CLazyClient::UpdateForcedZeroGravityPredictor()
{
	UpdateZeroGravityWarningHideTimer();
	if(!g_Config.m_LcZeroGravPredict || Client()->State() != IClient::STATE_ONLINE)
	{
		DeactivateForcedZeroGravity();
		return;
	}

	if(m_ZeroGravForcedState == ZERO_GRAV_FORCED_OFF)
	{
		ClearZeroGravityWarning(true);
		return;
	}

	if(m_ZeroGravForcedState == ZERO_GRAV_FORCED_WAIT_SECOND)
	{
		// Ignore ordinary input during the two-step spec sequence. The user has
		// to release and re-press movement keys around the pauses; those presses
		// must not cancel the prediction before the warning can appear. The bind
		// command itself still toggles forced mode off while waiting.
		ClearZeroGravityWarning(true);
		if(Client()->LocalTime() - m_ZeroGravForcedStartTime >= ZERO_GRAV_FORCED_SPEC_DELAY)
		{
			Console()->ExecuteLine("say /spec", IConsole::CLIENT_ID_UNSPECIFIED);
			m_ZeroGravForcedState = ZERO_GRAV_FORCED_ACTIVE;
			m_ZeroGravForcedStartTime = 0.0f;
			m_ZeroGravForcedInputGraceUntil = Client()->LocalTime() + 0.25f;
			ResetForcedZeroGravityInputState();
			if(m_ZeroGravForcedSeconds >= 0.0f)
			{
				m_ZeroGravWarning = true;
				m_ZeroGravSeconds = m_ZeroGravForcedSeconds;
				m_ZeroGravWarningHideTimer = 0.0f;
			}
			else
				ClearZeroGravityWarning(true);
		}
		return;
	}

	if(m_ZeroGravForcedState != ZERO_GRAV_FORCED_ACTIVE)
		return;

	// Keep the warning visible for a moment after the spec sequence so keys the
	// user presses to regain control do not clear it on the first frame. The
	// input state is still updated, so those keys do not count after the grace.
	const bool InputPressed = ForcedZeroGravityInputPressed();
	if(Client()->LocalTime() >= m_ZeroGravForcedInputGraceUntil && InputPressed)
	{
		DeactivateForcedZeroGravity();
		return;
	}

	if(m_ZeroGravForcedSeconds < 0.0f)
	{
		ClearZeroGravityWarning(false);
		return;
	}

	m_ZeroGravWarning = true;
	m_ZeroGravSeconds = m_ZeroGravForcedSeconds;
	m_ZeroGravWarningHideTimer = 0.0f;
}

void CLazyClient::UpdateZeroGravityPredictor()
{
	const CGameClient::CSnapState &Snap = GameClient()->m_Snap;
	if(g_Config.m_LcZeroGravMode == 1)
	{
		// Do not let Prediction mode resume with a stale hold history after
		// forced mode has been used.
		m_ZeroGravCheckTimer = 0.0f;
		m_ZeroGravHistoryCount = 0;
		m_ZeroGravHistoryNext = 0;
		m_ZeroGravMissedChecks = 0;
		m_ZeroGravFastChecks = 0;
		m_ZeroGravSpeedrunCooldown = 0;
		m_ZeroGravHasHookPos = false;
		if(g_Config.m_LcZeroGravPredict && m_ZeroGravForcedState == ZERO_GRAV_FORCED_OFF)
		{
			m_ZeroGravForcedCandidateCheck -= Client()->RenderFrameTime();
			if(m_ZeroGravForcedCandidateCheck <= 0.0f)
			{
				m_ZeroGravForcedCandidateCheck = 0.05f;
				CaptureForcedZeroGravityCandidate();
			}
		}
		UpdateForcedZeroGravityPredictor();
		return;
	}
	if(m_ZeroGravForcedState != ZERO_GRAV_FORCED_OFF)
		DeactivateForcedZeroGravity();
	UpdateZeroGravityWarningHideTimer();

	if(!g_Config.m_LcZeroGravPredict || Client()->State() != IClient::STATE_ONLINE)
	{
		ClearZeroGravityWarning(true);
		m_ZeroGravCheckTimer = 0.0f;
		m_ZeroGravHistoryCount = 0;
		m_ZeroGravHistoryNext = 0;
		m_ZeroGravTarget = -1;
		m_ZeroGravMissedChecks = 0;
		m_ZeroGravFastChecks = 0;
		m_ZeroGravSpeedrunCooldown = 0;
		return;
	}

	const int TargetId = FindZeroGravityTarget();
	if(TargetId < 0 || TargetId >= MAX_CLIENTS || !Snap.m_aCharacters[TargetId].m_Active)
	{
		ClearZeroGravityWarning(true);
		m_ZeroGravCheckTimer = 0.0f;
		m_ZeroGravHistoryCount = 0;
		m_ZeroGravHistoryNext = 0;
		m_ZeroGravTarget = -1;
		m_ZeroGravMissedChecks = 0;
		m_ZeroGravFastChecks = 0;
		m_ZeroGravSpeedrunCooldown = 0;
		return;
	}

	if(TargetId != m_ZeroGravTarget)
	{
		m_ZeroGravTarget = TargetId;
		ClearZeroGravityWarning(true);
		m_ZeroGravHistoryCount = 0;
		m_ZeroGravHistoryNext = 0;
		m_ZeroGravMissedChecks = 0;
		m_ZeroGravFastChecks = 0;
		m_ZeroGravSpeedrunCooldown = 0;
	}

	const bool TargetIsLocal = !Snap.m_SpecInfo.m_Active && TargetId == Snap.m_LocalClientId;
	CCharacterCore Core;
	const CNetObj_Character *pCharacter = nullptr;
	if(TargetIsLocal)
	{
		Core = GameClient()->m_PredictedChar;
		pCharacter = Snap.m_pLocalCharacter;
	}
	else
	{
		const CGameClient::CSnapState::CCharacterInfo &CharInfo = Snap.m_aCharacters[TargetId];
		Core.Reset();
		Core.SetCoreWorld(nullptr, Collision(), nullptr);
		Core.Read(&CharInfo.m_Cur);
		if(CharInfo.m_HasExtendedData)
			Core.ReadDDNet(&CharInfo.m_ExtendedData);
		Core.m_Tuning = GameClient()->m_aTuning[g_Config.m_ClDummy];
		Core.m_Input.m_Direction = Core.m_Direction;
		pCharacter = &CharInfo.m_Cur;
	}

	if(!pCharacter)
	{
		ClearZeroGravityWarning(true);
		m_ZeroGravCheckTimer = 0.0f;
		m_ZeroGravHistoryCount = 0;
		m_ZeroGravHistoryNext = 0;
		m_ZeroGravTarget = -1;
		m_ZeroGravMissedChecks = 0;
		m_ZeroGravFastChecks = 0;
		m_ZeroGravSpeedrunCooldown = 0;
		return;
	}

	const bool SnapshotHooked = pCharacter->m_HookState == HOOK_GRABBED;
	if((Core.m_HookState != HOOK_GRABBED && !SnapshotHooked) || Core.HookedPlayer() != -1 ||
		Core.m_FreezeEnd > 0 || Core.m_DeepFrozen || Core.m_LiveFrozen || Core.m_IsInFreeze ||
		Core.m_Super || Core.m_Invincible)
	{
		ClearZeroGravityWarning(true);
		m_ZeroGravCheckTimer = 0.0f;
		m_ZeroGravHistoryCount = 0;
		m_ZeroGravHistoryNext = 0;
		m_ZeroGravMissedChecks = 0;
		m_ZeroGravFastChecks = 0;
		m_ZeroGravSpeedrunCooldown = 0;
		return;
	}

	m_ZeroGravCheckTimer -= Client()->RenderFrameTime();
	if(m_ZeroGravCheckTimer > 0.0f)
		return;
	m_ZeroGravCheckTimer = 0.05f;

	const vec2 HookPos = Core.m_HookState == HOOK_GRABBED ? Core.m_HookPos : vec2(pCharacter->m_HookX, pCharacter->m_HookY);
	const vec2 HookDelta = HookPos - Core.m_Pos;
	// For the local tee we know the real held keys. `Core.m_Direction` is only
	// the last nonzero direction, so using it would keep simulating a key that
	// has already been released and over-predict freezes near the wall.
	const CNetObj_PlayerInput *pCurrentInput = TargetIsLocal ? &GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy] : nullptr;
	const int HoldDirection = pCurrentInput ? pCurrentInput->m_Direction : Core.m_Direction;
	if(m_ZeroGravHasHookPos && distance(HookPos, m_ZeroGravHookPos) > ZERO_GRAV_HOOK_MOVE_DISTANCE)
	{
		// A rehook is a new hold. Do not carry the old countdown across it.
		ClearZeroGravityWarning(true);
		m_ZeroGravHistoryCount = 0;
		m_ZeroGravHistoryNext = 0;
		m_ZeroGravMissedChecks = 0;
		m_ZeroGravFastChecks = 0;
		m_ZeroGravSpeedrunCooldown = 0;
	}
	m_ZeroGravHookPos = HookPos;
	m_ZeroGravHasHookPos = true;
	// The gores "zero gravity" hold is a wall hook with the opposing
	// movement key held, so the hook drag and air control cancel out.
	const bool ZeroGravityHold = Core.HookedPlayer() == -1 &&
		HoldDirection != 0 &&
		HookDelta.x * HoldDirection < 0.0f &&
		std::abs(HookDelta.x) > 8.0f && length(HookDelta) > 46.0f &&
		!Collision()->IsOnGround(Core.m_Pos, CCharacterCore::PhysicalSize());

	if(!ZeroGravityHold)
	{
		// A swaying hold can briefly fail a gate. Keep the last warning for
		// a few checks instead of flickering off and rebuilding the history.
		if(m_ZeroGravHistoryCount > 0 && m_ZeroGravMissedChecks < ZERO_GRAV_HOLD_GRACE_CHECKS)
		{
			m_ZeroGravMissedChecks++;
			return;
		}
		ClearZeroGravityWarning(false);
		m_ZeroGravHistoryCount = 0;
		m_ZeroGravHistoryNext = 0;
		m_ZeroGravMissedChecks = 0;
		m_ZeroGravFastChecks = 0;
		m_ZeroGravSpeedrunCooldown = 0;
		return;
	}

	m_aZeroGravHistory[m_ZeroGravHistoryNext] = Core.m_Pos;
	m_ZeroGravHistoryNext = (m_ZeroGravHistoryNext + 1) % ZERO_GRAV_HISTORY_SIZE;
	m_ZeroGravHistoryCount = std::min(m_ZeroGravHistoryCount + 1, ZERO_GRAV_HISTORY_SIZE);

	if(m_ZeroGravHistoryCount < ZERO_GRAV_HISTORY_SIZE)
	{
		ClearZeroGravityWarning(false);
		m_ZeroGravMissedChecks = 0;
		return;
	}

	// Only start predicting when the tee has stayed inside the configured
	// hold area for the last two seconds, which filters out fast map traversal.
	bool StayedInArea = true;
	const float HoldRadius = (float)g_Config.m_LcZeroGravHoldTiles * ZERO_GRAV_TILE_SIZE;
	float MinX = m_aZeroGravHistory[0].x, MaxX = MinX;
	float MinY = m_aZeroGravHistory[0].y, MaxY = MinY;
	for(const vec2 &Pos : m_aZeroGravHistory)
	{
		MinX = std::min(MinX, Pos.x);
		MaxX = std::max(MaxX, Pos.x);
		MinY = std::min(MinY, Pos.y);
		MaxY = std::max(MaxY, Pos.y);
	}
	StayedInArea = MaxX - MinX <= HoldRadius * 2.0f && MaxY - MinY <= HoldRadius * 2.0f;

	if(!StayedInArea)
	{
		// Remove the new outlier. A single spike should not poison the whole
		// history window, but sustained movement still clears it after grace.
		m_ZeroGravHistoryNext = (m_ZeroGravHistoryNext + ZERO_GRAV_HISTORY_SIZE - 1) % ZERO_GRAV_HISTORY_SIZE;
		m_ZeroGravHistoryCount--;
		if(m_ZeroGravMissedChecks < ZERO_GRAV_HOLD_GRACE_CHECKS)
		{
			m_ZeroGravMissedChecks++;
			return;
		}
		ClearZeroGravityWarning(false);
		m_ZeroGravHistoryCount = 0;
		m_ZeroGravHistoryNext = 0;
		m_ZeroGravMissedChecks = 0;
		m_ZeroGravFastChecks = 0;
		m_ZeroGravSpeedrunCooldown = 0;
		return;
	}
	m_ZeroGravMissedChecks = 0;

	// Anti-speedrun guard: a real zero-gravity hold swings within the hold
	// area, while a speedrun traverses it with sustained horizontal velocity.
	// Suppress the warning only after the speed stays high for a few checks,
	// and keep suppressing briefly after the tee slows down.
	if(g_Config.m_LcZeroGravSpeedrunGuard)
	{
		const float HSpeed = std::abs(Core.m_Vel.x) * (float)Client()->GameTickSpeed() / ZERO_GRAV_TILE_SIZE;
		if(HSpeed > (float)g_Config.m_LcZeroGravSpeedrunSpeed)
		{
			m_ZeroGravFastChecks = std::min(m_ZeroGravFastChecks + 1, ZERO_GRAV_SPEEDRUN_FAST_CHECKS);
			if(m_ZeroGravFastChecks >= ZERO_GRAV_SPEEDRUN_FAST_CHECKS)
				m_ZeroGravSpeedrunCooldown = ZERO_GRAV_SPEEDRUN_COOLDOWN_CHECKS;
		}
		else
		{
			m_ZeroGravFastChecks = 0;
		}

		if(m_ZeroGravSpeedrunCooldown > 0)
		{
			m_ZeroGravSpeedrunCooldown--;
			ClearZeroGravityWarning(false);
			return;
		}
	}
	else
	{
		m_ZeroGravFastChecks = 0;
		m_ZeroGravSpeedrunCooldown = 0;
	}

	const int TickSpeed = std::max(1, Client()->GameTickSpeed());
	const int MaxTicks = std::max(1, g_Config.m_LcZeroGravPredictTime) * TickSpeed;

	CCharacterCore Sim = Core;
	Sim.SetCoreWorld(nullptr, Collision(), nullptr);
	Sim.m_Input.m_Hook = pCurrentInput ? pCurrentInput->m_Hook : 1;
	Sim.m_Input.m_Direction = HoldDirection;
	if(pCurrentInput)
		Sim.m_Input.m_Jump = pCurrentInput->m_Jump;
	if(Sim.m_HookState != HOOK_GRABBED && SnapshotHooked)
	{
		// The predicted core can be a tick behind the snapshot. Keep using
		// the snapshot's hook position so the simulation stays attached.
		Sim.m_HookState = HOOK_GRABBED;
		Sim.m_HookPos = vec2(pCharacter->m_HookX, pCharacter->m_HookY);
		Sim.m_HookTick = 0;
		Sim.SetHookedPlayer(-1);
	}

	float HitSeconds = -1.0f;
	vec2 PrevPos = Sim.m_Pos;
	for(int Tick = 1; Tick <= MaxTicks; Tick++)
	{
		Sim.Tick(true, false);
		Sim.Move();
		Sim.Quantize();

		if(PathHitsFreeze(Collision(), PrevPos, Sim.m_Pos))
		{
			HitSeconds = Tick / (float)TickSpeed;
			break;
		}
		PrevPos = Sim.m_Pos;
	}

	if(HitSeconds < 0.0f)
	{
		ClearZeroGravityWarning(false);
		return;
	}

	m_ZeroGravWarning = true;
	m_ZeroGravSeconds = HitSeconds;
	m_ZeroGravWarningHideTimer = 0.0f;
}

void CLazyClient::RenderZeroGravityWarning()
{
	if(!g_Config.m_LcZeroGravPredict || !m_ZeroGravWarning ||
		Client()->State() != IClient::STATE_ONLINE ||
		GameClient()->m_Menus.IsActive() || GameClient()->m_Scoreboard.IsActive())
		return;

	const CScreenRect OldScreen = Graphics()->GetScreen();
	const float Width = 300.0f * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(Width, 300.0f);

	char aCountdown[96];
	const bool Freeview = GameClient()->m_Snap.m_SpecInfo.m_Active &&
		GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW &&
		m_ZeroGravTarget >= 0 && m_ZeroGravTarget < MAX_CLIENTS;
	if(Freeview)
		str_format(aCountdown, sizeof(aCountdown), Localize("%s: Freeze in %.1fs"), GameClient()->m_aClients[m_ZeroGravTarget].m_aName, m_ZeroGravSeconds);
	else
		str_format(aCountdown, sizeof(aCountdown), Localize("Freeze in %.1fs"), m_ZeroGravSeconds);

	const float CountdownSize = 10.5f;
	const float CountdownWidth = TextRender()->TextWidth(CountdownSize, aCountdown);
	const float BoxWidth = std::max(CountdownWidth + 14.0f, 96.0f);
	CUIRect Box = {Width / 2.0f - BoxWidth / 2.0f, 32.0f, BoxWidth, 22.0f};
	Box.Draw(ColorRGBA(0.55f, 0.04f, 0.03f, 0.86f), IGraphics::CORNER_ALL, 3.0f);
	Box.DrawOutline(ColorRGBA(1.0f, 0.35f, 0.25f, 0.9f));

	TextRender()->TextColor(ColorRGBA(1.0f, 0.92f, 0.88f, 1.0f));
	Ui()->DoLabel(&Box, aCountdown, CountdownSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	Graphics()->MapScreen(OldScreen);
}

void CLazyClient::ResetTelemetry()
{
	m_TelemetryFrameCount = 0;
	m_TelemetryFrameNext = 0;
	m_TelemetryStatsTimer = 0.0f;
	m_TelemetryFpsAvg = 0.0f;
	m_TelemetryFps1Pct = 0.0f;
	m_TelemetryFps01Pct = 0.0f;
	m_TelemetryPing = 0.0f;
	m_TelemetryJitter = 0.0f;
	m_TelemetryLastPing = -1.0f;
	m_TelemetryPacketLoss = 0.0f;
	m_TelemetrySnapshots = 0;
	m_TelemetrySnapshotRate = 0.0f;
	m_TelemetryPrevRecvVital = 0;
	m_TelemetryPrevLostVital = 0;
	m_TelemetryPrevResend = 0;
	for(int i = 0; i < TELEMETRY_LOSS_SAMPLES; i++)
	{
		m_aTelemetryLossRecv[i] = 0;
		m_aTelemetryLossLost[i] = 0;
	}
	m_TelemetryLossIndex = 0;
}

void CLazyClient::UpdateTelemetry()
{
	if(!g_Config.m_LcTelemetry)
	{
		ResetTelemetry();
		return;
	}

	const float FrameTime = Client()->RenderFrameTime();
	const bool RecordStats = Client()->State() == IClient::STATE_ONLINE && !GameClient()->m_Menus.IsActive();
	if(RecordStats && FrameTime > 0.0f && FrameTime < 1.0f)
	{
		m_aTelemetryFrameTimes[m_TelemetryFrameNext] = FrameTime;
		m_TelemetryFrameNext = (m_TelemetryFrameNext + 1) % TELEMETRY_FRAME_SAMPLES;
		m_TelemetryFrameCount = std::min(m_TelemetryFrameCount + 1, TELEMETRY_FRAME_SAMPLES);
	}

	m_TelemetryStatsTimer -= RecordStats ? std::clamp(FrameTime, 0.0f, 1.0f) : 0.0f;
	if(m_TelemetryStatsTimer > 0.0f)
		return;
	m_TelemetryStatsTimer += TELEMETRY_STATS_INTERVAL;
	if(m_TelemetryStatsTimer < 0.0f)
		m_TelemetryStatsTimer = 0.0f;

	if(m_TelemetryFrameCount > 0)
	{
		float TotalFrameTime = 0.0f;
		for(int i = 0; i < m_TelemetryFrameCount; i++)
		{
			m_aTelemetrySorted[i] = m_aTelemetryFrameTimes[i];
			TotalFrameTime += m_aTelemetryFrameTimes[i];
		}
		std::sort(m_aTelemetrySorted, m_aTelemetrySorted + m_TelemetryFrameCount);

		m_TelemetryFpsAvg = TotalFrameTime > 0.0f ? m_TelemetryFrameCount / TotalFrameTime : 0.0f;
		const int Index1Pct = std::min(m_TelemetryFrameCount - 1, (int)(m_TelemetryFrameCount * 0.99f));
		const int Index01Pct = std::min(m_TelemetryFrameCount - 1, (int)(m_TelemetryFrameCount * 0.999f));
		m_TelemetryFps1Pct = 1.0f / std::max(0.0001f, m_aTelemetrySorted[Index1Pct]);
		m_TelemetryFps01Pct = 1.0f / std::max(0.0001f, m_aTelemetrySorted[Index01Pct]);
	}

	if(GameClient()->m_Snap.m_pLocalInfo)
		m_TelemetryPing = (float)std::max(0, GameClient()->m_Snap.m_pLocalInfo->m_Latency);

	const SClientNetStats Stats = Client()->GetNetStats(g_Config.m_ClDummy);
	const bool NetStatsReset = Stats.m_RecvVitalChunks < m_TelemetryPrevRecvVital ||
		Stats.m_LostVitalChunks < m_TelemetryPrevLostVital ||
		Stats.m_ResendRequests < m_TelemetryPrevResend;
	if(NetStatsReset)
	{
		for(int i = 0; i < TELEMETRY_LOSS_SAMPLES; i++)
		{
			m_aTelemetryLossRecv[i] = 0;
			m_aTelemetryLossLost[i] = 0;
		}
		m_TelemetryLossIndex = 0;
	}
	else
	{
		const uint64_t Received = Stats.m_RecvVitalChunks - m_TelemetryPrevRecvVital;
		const uint64_t LostChunks = Stats.m_LostVitalChunks - m_TelemetryPrevLostVital;
		const uint64_t ResendRequests = Stats.m_ResendRequests - m_TelemetryPrevResend;
		m_aTelemetryLossRecv[m_TelemetryLossIndex] = Received;
		// Prefer the chunk-gap estimate; resend requests are a fallback for
		// cases where the gap was filled before it could be counted.
		m_aTelemetryLossLost[m_TelemetryLossIndex] = LostChunks > 0 ? LostChunks : ResendRequests;
		m_TelemetryLossIndex = (m_TelemetryLossIndex + 1) % TELEMETRY_LOSS_SAMPLES;
	}
	m_TelemetryPrevRecvVital = Stats.m_RecvVitalChunks;
	m_TelemetryPrevLostVital = Stats.m_LostVitalChunks;
	m_TelemetryPrevResend = Stats.m_ResendRequests;

	uint64_t WindowReceived = 0;
	uint64_t WindowLost = 0;
	for(int i = 0; i < TELEMETRY_LOSS_SAMPLES; i++)
	{
		WindowReceived += m_aTelemetryLossRecv[i];
		WindowLost += m_aTelemetryLossLost[i];
	}
	m_TelemetryPacketLoss = WindowReceived + WindowLost > 0 ? 100.0f * (float)WindowLost / (float)(WindowReceived + WindowLost) : 0.0f;

	m_TelemetrySnapshotRate = (float)m_TelemetrySnapshots;
	m_TelemetrySnapshots = 0;
}

void CLazyClient::RenderTelemetry()
{
	if(!g_Config.m_LcTelemetry || Client()->State() != IClient::STATE_ONLINE ||
		GameClient()->m_Menus.IsActive())
		return;

	char aLines[12][96];
	int NumLines = 0;
	auto AddLine = [&](const char *pLine) {
		if(NumLines < (int)(sizeof(aLines) / sizeof(aLines[0])))
			str_copy(aLines[NumLines++], pLine, sizeof(aLines[0]));
	};

	char aBuf[96];
	const float AverageFrameTime = std::max(0.0001f, Client()->FrameTimeAverage());
	if(g_Config.m_LcTelemetryFps)
	{
		str_format(aBuf, sizeof(aBuf), "FPS: %d", round_to_int(1.0f / AverageFrameTime));
		AddLine(aBuf);
	}
	if(g_Config.m_LcTelemetryFpsAvg)
	{
		str_format(aBuf, sizeof(aBuf), "Avg FPS: %d", round_to_int(m_TelemetryFpsAvg));
		AddLine(aBuf);
	}
	if(g_Config.m_LcTelemetryFps1Pct)
	{
		str_format(aBuf, sizeof(aBuf), "1%% low: %d", round_to_int(m_TelemetryFps1Pct));
		AddLine(aBuf);
	}
	if(g_Config.m_LcTelemetryFps01Pct)
	{
		str_format(aBuf, sizeof(aBuf), "0.1%% low: %d", round_to_int(m_TelemetryFps01Pct));
		AddLine(aBuf);
	}
	if(g_Config.m_LcTelemetryFrametime)
	{
		str_format(aBuf, sizeof(aBuf), "Frame: %.2f ms", AverageFrameTime * 1000.0f);
		AddLine(aBuf);
	}
	if(g_Config.m_LcTelemetryPing)
	{
		str_format(aBuf, sizeof(aBuf), "Ping: %d ms", round_to_int(m_TelemetryPing));
		AddLine(aBuf);
	}
	if(g_Config.m_LcTelemetryJitter)
	{
		str_format(aBuf, sizeof(aBuf), "Jitter: %.1f ms", m_TelemetryJitter);
		AddLine(aBuf);
	}
	if(g_Config.m_LcTelemetryLoss)
	{
		str_format(aBuf, sizeof(aBuf), "Loss: %.2f%%", m_TelemetryPacketLoss);
		AddLine(aBuf);
	}
	if(g_Config.m_LcTelemetryPrediction)
	{
		str_format(aBuf, sizeof(aBuf), "Prediction: %d ms", std::max(0, Client()->GetPredictionTime()));
		AddLine(aBuf);
	}
	if(g_Config.m_LcTelemetrySnaps)
	{
		str_format(aBuf, sizeof(aBuf), "Snaps: %d/s", round_to_int(m_TelemetrySnapshotRate));
		AddLine(aBuf);
	}

	if(NumLines == 0)
		return;

	const CScreenRect OldScreen = Graphics()->GetScreen();
	const float Width = 300.0f * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(Width, 300.0f);

	const float FontSize = 9.5f;
	const float LineHeight = 11.0f;
	float MaxTextWidth = 0.0f;
	for(int i = 0; i < NumLines; i++)
		MaxTextWidth = std::max(MaxTextWidth, TextRender()->TextWidth(FontSize, aLines[i]));

	const float BoxWidth = MaxTextWidth + 12.0f;
	const float BoxHeight = NumLines * LineHeight + 8.0f;
	CUIRect Box = {Width - BoxWidth - 6.0f, 34.0f, BoxWidth, BoxHeight};
	Box.Draw(ColorRGBA(0.03f, 0.04f, 0.06f, 0.72f), IGraphics::CORNER_ALL, 3.0f);
	Box.DrawOutline(ColorRGBA(0.45f, 0.65f, 0.85f, 0.35f));

	TextRender()->TextColor(ColorRGBA(0.9f, 0.95f, 1.0f, 0.95f));
	float y = Box.y + 4.0f;
	for(int i = 0; i < NumLines; i++)
	{
		TextRender()->Text(Box.x + 6.0f, y, FontSize, aLines[i]);
		y += LineHeight;
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	Graphics()->MapScreen(OldScreen);
}

void CLazyClient::OnRender()
{
	RenderZeroGravityWarning();
	RenderTelemetry();
}

void CLazyClient::OnUpdate()
{
	const bool GoresEnabled = g_Config.m_LcGoresMode != 0;
	if(!m_GoresInitialized)
	{
		// Config files are executed after OnConsoleInit, so handle the initial
		// state here instead.
		m_GoresInitialized = true;
		if(GoresEnabled)
			BindGunKeyForced();
		else
			RestoreStaleGunKeyBind();
		m_GoresWasEnabled = GoresEnabled;
	}
	else if(GoresEnabled && !m_GoresWasEnabled)
	{
		BindGunKeyForced();
		m_GoresWasEnabled = true;
	}
	else if(!GoresEnabled && m_GoresWasEnabled)
	{
		RestoreGunKey();
		m_GoresWasEnabled = false;
	}

	const bool ForcedZeroGravEnabled = g_Config.m_LcZeroGravMode == 1;
	if(!m_ZeroGravKeyInitialized)
	{
		m_ZeroGravKeyInitialized = true;
		if(ForcedZeroGravEnabled)
			BindZeroGravKeyForced();
		else
			RestoreStaleZeroGravKeyBind();
		m_ZeroGravWasForced = ForcedZeroGravEnabled;
	}
	else if(ForcedZeroGravEnabled && !m_ZeroGravWasForced)
	{
		BindZeroGravKeyForced();
		m_ZeroGravWasForced = true;
	}
	else if(!ForcedZeroGravEnabled && m_ZeroGravWasForced)
	{
		RestoreZeroGravKey();
		m_ZeroGravWasForced = false;
	}

	if(m_ZeroGravForcedBindHeld && !AnyForcedZeroGravityBindKeyPressed())
		m_ZeroGravForcedBindHeld = false;

	UpdateClientLogoState();
	UpdateBestClientList();
	UpdateAutoMargin();
	UpdateAntiping();
	UpdateAspectConfirm();
	UpdateZeroGravityPredictor();
	UpdateTelemetry();
}
