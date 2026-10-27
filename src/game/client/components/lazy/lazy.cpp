#include "lazy.h"

#include "lazy_ccid.h"

#include <base/io.h>
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
#include <engine/shared/linereader.h>
#include <engine/storage.h>
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
#include <cstdio>
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
constexpr float TELEMETRY_STATS_INTERVAL = 1.0f;
constexpr float AUTO_KILL_MOVE_TOLERANCE = 8.0f;
// Same value as CChat's private SERVER_MSG: chat messages sent by the server.
constexpr int SERVER_CHAT_CLIENT_ID = -1;

enum EInviteStatus
{
	INVITE_STATUS_UNKNOWN = -1,
	INVITE_STATUS_DENY = 0,
	INVITE_STATUS_ASK = 1,
	INVITE_STATUS_APPROVE = 2,
};

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

bool IsFreezeTileAt(const CCollision *pCollision, vec2 Pos)
{
	if(!pCollision)
		return false;
	const int Index = pCollision->GetMapIndex(Pos);
	if(Index < 0)
		return false;
	return IsFreezeTile(pCollision->GetTileIndex(Index)) ||
		IsFreezeTile(pCollision->GetFrontTileIndex(Index));
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

const char *ZoomCommand(int Index)
{
	return Index == 0 ? "lc_zoom_in" : "lc_zoom_out";
}

const char *ZoomKeyName(int Index)
{
	return Index == 0 ? g_Config.m_LcZoomInKey : g_Config.m_LcZoomOutKey;
}

char *ZoomPrevKey(int Index)
{
	return Index == 0 ? g_Config.m_LcZoomInKeyPrev : g_Config.m_LcZoomOutKeyPrev;
}
} // namespace

void CLazyClient::OnConsoleInit()
{
	Console()->Register("+lc_gun", "", CFGFLAG_CLIENT, ConGunFire, this, "Fire the gun in gores mode without using the hammer");
	Console()->Register("+lc_zerograv_forced", "", CFGFLAG_CLIENT, ConZeroGravForced, this, "Toggle the zero-gravity freeze predictor");
	Console()->Register("lc_zoom_in", "", CFGFLAG_CLIENT, ConZoomIn, this, "Zoom in by lc_zoom_step levels");
	Console()->Register("lc_zoom_out", "", CFGFLAG_CLIENT, ConZoomOut, this, "Zoom out by lc_zoom_step levels");
	Console()->Register("lc_team_lock", "", CFGFLAG_CLIENT, ConTeamLock, this, "Lock your current team immediately");
	Console()->Register("lc_invite_approve", "r[name]", CFGFLAG_CLIENT, ConInviteApprove, this, "Auto-invite this player when they whisper !invite");
	Console()->Register("lc_invite_ask", "r[name]", CFGFLAG_CLIENT, ConInviteAsk, this, "Ask before inviting this player (show a chat notice)");
	Console()->Register("lc_invite_deny", "r[name]", CFGFLAG_CLIENT, ConInviteDeny, this, "Ignore !invite whispers from this player");
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
	m_ZeroGravForcedEnabled = false;
	m_ZeroGravForcedStatusOn = false;
	m_ZeroGravForcedStatusUntil = 0.0f;
	m_ZeroGravForcedBindHeld = false;
	ClearZeroGravityWarning(true);
	ResetZeroGravityTracking();
	ResetAutoMarginState();
	ResetTelemetry();
	ResetAutoTeamLockState();
	ResetAutoKillState();
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
	m_ZeroGravForcedEnabled = false;
	m_ZeroGravForcedStatusOn = false;
	m_ZeroGravForcedStatusUntil = 0.0f;
	ClearZeroGravityWarning(true);
	ResetZeroGravityTracking();
	ResetTelemetry();
	ResetAutoTeamLockState();
	ResetAutoKillState();
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

bool CLazyClient::HasInputLead() const
{
	return InputOffsetTicks() > 0.0f;
}

void CLazyClient::ResetSnapTap(int Dummy)
{
	m_aSnapApplied[Dummy] = 0;
	m_aSnapLastPressed[Dummy] = 0;
	m_aSnapLastPressedTime[Dummy] = 0;
	m_aSnapPending[Dummy] = 0;
}

void CLazyClient::OnMovementKey(bool Left, bool Pressed)
{
	if(!g_Config.m_LcSnapTap)
		return;

	const int Dummy = g_Config.m_ClDummy;
	if(Pressed)
	{
		m_aSnapLastPressed[Dummy] = Left ? -1 : 1;
		m_aSnapLastPressedTime[Dummy] = time_get();
		return;
	}

	if(GameClient()->m_Chat.IsActive() || GameClient()->m_Menus.IsActive() || GameClient()->m_GameConsole.IsActive())
		return;

	// Direction is sampled once per simulation tick. A tap that starts and ends
	// between two ticks would otherwise be invisible, so remember its direction
	// and apply it for one tick.
	const int Direction = Left ? -1 : 1;
	if(GameClient()->m_Controls.m_aLastData[Dummy].m_Direction != Direction)
		m_aSnapPending[Dummy] = Direction;
}

int CLazyClient::ResolveMovementDirection(int Dummy, bool LeftPressed, bool RightPressed, bool ConsumePending)
{
	int Direction;
	if(LeftPressed == RightPressed)
	{
		if(!LeftPressed)
		{
			m_aSnapApplied[Dummy] = 0;
			Direction = 0;
		}
		else if(!g_Config.m_LcSnapTap)
		{
			Direction = 0;
		}
		else
		{
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

			Direction = m_aSnapApplied[Dummy];
		}
	}
	else
	{
		m_aSnapApplied[Dummy] = LeftPressed ? -1 : 1;
		Direction = m_aSnapApplied[Dummy];
	}

	// A short tap is only consumed by the real input path (SnapInput), so the
	// fast-input prediction still resolves from the live key state.
	if(ConsumePending && m_aSnapPending[Dummy] != 0)
	{
		Direction = m_aSnapPending[Dummy];
		m_aSnapPending[Dummy] = 0;
	}

	return Direction;
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

void CLazyClient::ConZoomIn(IConsole::IResult *pResult, void *pUserData)
{
	((CLazyClient *)pUserData)->ZoomByConfiguredStep(true);
}

void CLazyClient::ConZoomOut(IConsole::IResult *pResult, void *pUserData)
{
	((CLazyClient *)pUserData)->ZoomByConfiguredStep(false);
}

void CLazyClient::ZoomByConfiguredStep(bool ZoomIn)
{
	const float Step = (float)std::clamp(g_Config.m_LcZoomStep, 1, 20);
	GameClient()->m_Camera.ZoomBySteps(ZoomIn ? Step : -Step);
}

void CLazyClient::ConTeamLock(IConsole::IResult *pResult, void *pUserData)
{
	((CLazyClient *)pUserData)->LockTeamNow();
}

void CLazyClient::ConInviteApprove(IConsole::IResult *pResult, void *pUserData)
{
	CLazyClient *pSelf = (CLazyClient *)pUserData;
	pSelf->AddInviteEntry("approve", pResult->GetString(0));
}

void CLazyClient::ConInviteAsk(IConsole::IResult *pResult, void *pUserData)
{
	CLazyClient *pSelf = (CLazyClient *)pUserData;
	pSelf->AddInviteEntry("ask", pResult->GetString(0));
}

void CLazyClient::ConInviteDeny(IConsole::IResult *pResult, void *pUserData)
{
	CLazyClient *pSelf = (CLazyClient *)pUserData;
	pSelf->AddInviteEntry("deny", pResult->GetString(0));
}

int CLazyClient::CurrentDDRaceTeam() const
{
	const int LocalId = GameClient()->m_aLocalIds[0];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return TEAM_FLOCK;
	if(Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalInfo)
		return TEAM_FLOCK;
	const int Team = GameClient()->m_Teams.Team(LocalId);
	if(Team <= TEAM_FLOCK || Team >= NUM_DDRACE_TEAMS)
		return TEAM_FLOCK;
	return Team;
}

void CLazyClient::LockTeamNow()
{
	ResetAutoTeamLockState();
	if(CurrentDDRaceTeam() == TEAM_FLOCK)
	{
		GameClient()->m_Chat.Echo("LazyClient: join a team before locking it.");
		return;
	}
	m_AutoTeamLockLocked = true;
	GameClient()->m_Chat.SendChat(0, "/lock");
}

void CLazyClient::ResetAutoTeamLockState()
{
	m_AutoTeamLockTeam = -1;
	m_AutoTeamLockPending = false;
	m_AutoTeamLockSeconds = 0.0f;
	m_AutoTeamLockLocked = false;
}

void CLazyClient::ResetAutoKillState()
{
	m_AutoKillTeam = -1;
	m_AutoKillSeconds = 0.0f;
	m_AutoKillFired = false;
	m_AutoKillHasPos = false;
	m_AutoKillAnchorPos = vec2(0.0f, 0.0f);
}

void CLazyClient::UpdateAutoKill()
{
	const bool Enabled = g_Config.m_LcAutoKillFrozen != 0;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	const int Team = CurrentDDRaceTeam();
	const bool Online = Client()->State() == IClient::STATE_ONLINE;

	bool Frozen = false;
	bool Alone = false;
	bool HasPos = false;
	bool InFreezeTile = false;
	vec2 Pos(0.0f, 0.0f);
	if(Online && Team != TEAM_FLOCK && LocalId >= 0 && LocalId < MAX_CLIENTS &&
		!GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		Frozen = GameClient()->m_aClients[LocalId].m_FreezeEnd > 0 ||
			GameClient()->m_aClients[LocalId].m_DeepFrozen;

		int NumInTeam = 0;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!GameClient()->m_Snap.m_apPlayerInfos[i])
				continue;
			if(GameClient()->m_Teams.Team(i) == Team)
				NumInTeam++;
		}
		Alone = NumInTeam == 1;

		HasPos = GameClient()->m_Snap.m_pLocalCharacter != nullptr;
		Pos = GameClient()->m_LocalCharacterPos;
		InFreezeTile = HasPos && IsFreezeTileAt(Collision(), Pos);
	}

	if(!Enabled || !Online || !Frozen || !Alone || !HasPos || !InFreezeTile)
	{
		ResetAutoKillState();
		return;
	}

	if(Team != m_AutoKillTeam)
	{
		ResetAutoKillState();
		m_AutoKillTeam = Team;
	}

	if(!m_AutoKillHasPos)
	{
		m_AutoKillAnchorPos = Pos;
		m_AutoKillHasPos = true;
		return;
	}

	// Frozen tees can still slide on ice. Only count time while the position
	// stays close to the anchor, so real movement restarts the timer.
	if(distance(Pos, m_AutoKillAnchorPos) > AUTO_KILL_MOVE_TOLERANCE)
	{
		m_AutoKillAnchorPos = Pos;
		m_AutoKillSeconds = 0.0f;
		return;
	}

	if(m_AutoKillFired)
		return;

	m_AutoKillSeconds += Client()->RenderFrameTime();
	const float Timeout = (float)std::clamp(g_Config.m_LcAutoKillFrozenTime, 1, 120);
	if(m_AutoKillSeconds < Timeout)
		return;

	// Kill once per freeze. The next respawn is not frozen, which resets the
	// state; if the server ignores the kill, do not spam it every tick.
	m_AutoKillFired = true;
	GameClient()->SendKill();
}

int CLazyClient::InviteStatus(const char *pName) const
{
	if(pName == nullptr || pName[0] == '\0')
		return INVITE_STATUS_UNKNOWN;

	CLineReader Reader;
	if(!Reader.OpenFile(Storage()->OpenFile(INVITES_FILE, IOFLAG_READ, IStorage::TYPE_SAVE)))
		return INVITE_STATUS_UNKNOWN;

	int Status = INVITE_STATUS_UNKNOWN;
	while(const char *pLine = Reader.Get())
	{
		const char *pTrimmed = str_skip_whitespaces_const(pLine);
		if(pTrimmed[0] == '\0' || pTrimmed[0] == '#')
			continue;

		char aStatus[16];
		if(sscanf(pTrimmed, "%15s", aStatus) != 1)
			continue;
		const char *pEntryName = str_trim_words(pTrimmed, 1);
		if(pEntryName[0] == '\0')
			continue;

		char aName[MAX_NAME_LENGTH];
		str_copy(aName, pEntryName, sizeof(aName));
		str_utf8_trim_right(aName);
		if(str_comp_nocase(aName, pName) != 0)
			continue;

		// Later lines win, so appending an entry overrides an older one.
		if(str_comp_nocase(aStatus, "approve") == 0)
			Status = INVITE_STATUS_APPROVE;
		else if(str_comp_nocase(aStatus, "deny") == 0)
			Status = INVITE_STATUS_DENY;
		else if(str_comp_nocase(aStatus, "ask") == 0)
			Status = INVITE_STATUS_ASK;
	}
	return Status;
}

void CLazyClient::AddInviteEntry(const char *pStatus, const char *pName)
{
	if(pStatus == nullptr || pName == nullptr)
		return;

	char aName[MAX_NAME_LENGTH];
	str_copy(aName, str_skip_whitespaces_const(pName), sizeof(aName));
	str_utf8_trim_right(aName);
	if(aName[0] == '\0')
		return;

	IOHANDLE File = Storage()->OpenFile(INVITES_FILE, IOFLAG_APPEND, IStorage::TYPE_SAVE);
	if(!File)
		return;
	char aLine[MAX_NAME_LENGTH + 32];
	str_format(aLine, sizeof(aLine), "%s %s\n", pStatus, aName);
	io_write(File, aLine, str_length(aLine));
	io_close(File);
}

void CLazyClient::HandleTeamInvite(int ClientId, const char *pMessage)
{
	if(Client()->State() != IClient::STATE_ONLINE || GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	const int LocalId = GameClient()->m_aLocalIds[0];
	if(ClientId == LocalId || ClientId == GameClient()->m_aLocalIds[1])
		return;

	// Only team members can be invited; no need to handle whispers while solo.
	const int Team = CurrentDDRaceTeam();
	if(Team == TEAM_FLOCK)
		return;
	if(GameClient()->m_Teams.Team(ClientId) == Team)
		return;
	if(GameClient()->m_aClients[ClientId].m_ChatIgnore)
		return;

	char aCommand[16];
	if(sscanf(str_skip_whitespaces_const(pMessage), "%15s", aCommand) != 1 ||
		str_comp_nocase(aCommand, "!invite") != 0)
		return;

	const char *pName = GameClient()->m_aClients[ClientId].m_aName;
	if(pName[0] == '\0')
		return;

	const int Status = InviteStatus(pName);
	char aBuf[256];
	if(Status == INVITE_STATUS_APPROVE)
	{
		str_format(aBuf, sizeof(aBuf), "/invite %s", pName);
		GameClient()->m_Chat.SendChat(0, aBuf);
		str_format(aBuf, sizeof(aBuf), Localize("Auto-invited %s to your team"), pName);
		GameClient()->m_Chat.Echo(aBuf);
		return;
	}
	if(Status == INVITE_STATUS_DENY)
		return;

	if(Status == INVITE_STATUS_UNKNOWN)
		AddInviteEntry("ask", pName);
	str_format(aBuf, sizeof(aBuf), Localize("%s is asking to join your team"), pName);
	GameClient()->m_Chat.Echo(aBuf);
}

void CLazyClient::OnChatMessage(int ClientId, int Team, const char *pMessage)
{
	if(pMessage == nullptr)
		return;

	if(Team == TEAM_WHISPER_RECV)
	{
		if(g_Config.m_LcInviteManager)
			HandleTeamInvite(ClientId, pMessage);
		return;
	}

	// The client cannot read the server's lock flag, but DDNet announces every
	// lock and unlock change with a server message in the team chat:
	//   "'%s' locked your team. ..." / "'%s' unlocked your team."
	// SendChatTarget marks these as team 0 (all) even though only the team
	// receives them, so only the server sender matters here. Requiring a server
	// message also stops players from faking the state in their own chat.
	if(ClientId != SERVER_CHAT_CLIENT_ID)
		return;

	const bool Unlocked = str_find(pMessage, " unlocked your team") != nullptr;
	// " unlocked your team" contains " locked your team", so check unlocked first.
	const bool Locked = !Unlocked && str_find(pMessage, " locked your team") != nullptr;
	if(!Locked && !Unlocked)
		return;

	const int MyTeam = CurrentDDRaceTeam();
	if(MyTeam == TEAM_FLOCK)
		return;
	m_AutoTeamLockTeam = MyTeam;

	if(Unlocked)
	{
		// Someone unlocked the team; stop considering it locked so the helper
		// can lock it again after the delay.
		m_AutoTeamLockLocked = false;
		m_AutoTeamLockPending = g_Config.m_LcAutoTeamLock != 0;
		m_AutoTeamLockSeconds = (float)std::clamp(g_Config.m_LcAutoTeamLockDelay, 5, 600);
		return;
	}

	m_AutoTeamLockLocked = true;
	m_AutoTeamLockPending = false;
}

void CLazyClient::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType != NETMSGTYPE_SV_CHAT)
		return;
	CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
	OnChatMessage(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage);
}

void CLazyClient::UpdateAutoTeamLock()
{
	const bool Enabled = g_Config.m_LcAutoTeamLock != 0;
	const int Team = CurrentDDRaceTeam();

	if(!Enabled || Client()->State() != IClient::STATE_ONLINE || Team == TEAM_FLOCK)
	{
		// Do not keep a countdown across leaving the team or disabling the option.
		if(m_AutoTeamLockPending || m_AutoTeamLockTeam != Team)
			ResetAutoTeamLockState();
		return;
	}

	if(Team != m_AutoTeamLockTeam)
	{
		// Joined a different team: the previous lock state does not apply to it.
		m_AutoTeamLockTeam = Team;
		m_AutoTeamLockLocked = false;
		m_AutoTeamLockPending = true;
		m_AutoTeamLockSeconds = (float)std::clamp(g_Config.m_LcAutoTeamLockDelay, 5, 600);
	}

	if(!m_AutoTeamLockPending || m_AutoTeamLockLocked)
		return;

	m_AutoTeamLockSeconds -= Client()->RenderFrameTime();
	if(m_AutoTeamLockSeconds > 0.0f)
		return;

	m_AutoTeamLockPending = false;
	m_AutoTeamLockLocked = true;
	GameClient()->m_Chat.SendChat(0, "/lock");
}

void CLazyClient::BindZoomKey(bool ZoomIn)
{
	BindZoomKeyForced(ZoomIn ? 0 : 1);
}

void CLazyClient::BindZoomKeyForced(int Index)
{
	const char *pKeyName = ZoomKeyName(Index);
	char *pPrevConfig = ZoomPrevKey(Index);

	if(pKeyName[0] == '\0')
	{
		if(m_aZoomKeyBound[Index])
			RestoreZoomKey(Index);
		else
			RestoreStaleZoomKeyBind(Index);
		return;
	}

	const int Key = Input()->FindKeyByName(pKeyName);
	if(Key == KEY_UNKNOWN)
		return;

	if(m_aZoomKeyBound[Index] && m_aZoomBoundKey[Index] == Key)
		return;
	if(m_aZoomKeyBound[Index])
		RestoreZoomKey(Index);

	// One key cannot run both zoom commands.
	const int Other = 1 - Index;
	if(m_aZoomKeyBound[Other] && m_aZoomBoundKey[Other] == Key)
		RestoreZoomKey(Other);

	const char *pCommand = ZoomCommand(Index);
	const char *pCurrentBind = GameClient()->m_Binds.Get(Key, KeyModifier::NONE);
	if(str_comp(pCurrentBind, pCommand) == 0)
	{
		// Still bound from a previous session; recover the original bind from the config.
		str_copy(m_aZoomPrevBind[Index], pPrevConfig, sizeof(m_aZoomPrevBind[Index]));
	}
	else
	{
		str_copy(m_aZoomPrevBind[Index], pCurrentBind, sizeof(m_aZoomPrevBind[Index]));
		str_copy(pPrevConfig, pCurrentBind, 128);
		GameClient()->m_Binds.Bind(Key, pCommand);
	}
	m_aZoomBoundKey[Index] = Key;
	m_aZoomKeyBound[Index] = true;
}

void CLazyClient::RestoreZoomKey(int Index)
{
	if(!m_aZoomKeyBound[Index])
		return;
	GameClient()->m_Binds.Bind(m_aZoomBoundKey[Index], m_aZoomPrevBind[Index]);
	ZoomPrevKey(Index)[0] = '\0';
	m_aZoomPrevBind[Index][0] = '\0';
	m_aZoomKeyBound[Index] = false;
	m_aZoomBoundKey[Index] = 0;
}

void CLazyClient::RestoreStaleZoomKeyBind(int Index)
{
	char *pPrevConfig = ZoomPrevKey(Index);
	if(pPrevConfig[0] == '\0')
		return;

	const int Key = Input()->FindKeyByName(ZoomKeyName(Index));
	if(Key == KEY_UNKNOWN)
	{
		pPrevConfig[0] = '\0';
		return;
	}

	if(str_comp(GameClient()->m_Binds.Get(Key, KeyModifier::NONE), ZoomCommand(Index)) == 0)
		GameClient()->m_Binds.Bind(Key, pPrevConfig);
	pPrevConfig[0] = '\0';
}

void CLazyClient::UpdateZoomKeyBinds()
{
	// Restore/bind the configured keys once on startup. Afterwards the
	// settings button applies changes explicitly, like the zero-gravity key,
	// so typing in the edit box does not bind half-finished key names.
	if(m_ZoomKeysInitialized)
		return;
	m_ZoomKeysInitialized = true;
	BindZoomKeyForced(0);
	BindZoomKeyForced(1);
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
	if(g_Config.m_LcGoresIgnoreHeavy && m_GoresHeavyWeapon)
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
	if(g_Config.m_LcGoresIgnoreHeavy && m_GoresHeavyWeapon)
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

void CLazyClient::ResetZeroGravityTracking()
{
	m_ZeroGravCheckTimer = 0.0f;
	m_ZeroGravHistoryCount = 0;
	m_ZeroGravHistoryNext = 0;
	m_ZeroGravMissedChecks = 0;
	m_ZeroGravFastChecks = 0;
	m_ZeroGravSpeedrunCooldown = 0;
	m_ZeroGravTarget = -1;
	m_ZeroGravHasHookPos = false;
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
	const int ForcedKey = Input()->FindKeyByName(g_Config.m_LcZeroGravForcedKey);
	if(ForcedKey >= KEY_MOUSE_1 && ForcedKey <= KEY_MOUSE_WHEEL_RIGHT && GameClient()->m_Scoreboard.IsActive())
		return;

	m_ZeroGravForcedEnabled = !m_ZeroGravForcedEnabled;
	m_ZeroGravForcedStatusOn = m_ZeroGravForcedEnabled;
	m_ZeroGravForcedStatusUntil = Client()->LocalTime() + 2.0f;

	// Forced mode is only a switch. Start the ordinary predictor from a clean
	// state so it has exactly the same warmup and rules as Prediction mode.
	ClearZeroGravityWarning(true);
	ResetZeroGravityTracking();
}

float CLazyClient::SimulateZeroGravityFreeze(const CCharacterCore &Core, int Hook, int Direction, int Jump, bool SnapshotHooked, vec2 SnapshotHookPos) const
{
	CCharacterCore Sim = Core;
	Sim.SetCoreWorld(nullptr, Collision(), nullptr);
	Sim.m_Input.m_Hook = Hook;
	Sim.m_Input.m_Direction = Direction;
	Sim.m_Input.m_Jump = Jump;
	if(Sim.m_HookState != HOOK_GRABBED && SnapshotHooked)
	{
		// The predicted core can be a tick behind the snapshot. Keep using
		// the snapshot's hook position so the simulation stays attached.
		Sim.m_HookState = HOOK_GRABBED;
		Sim.m_HookPos = SnapshotHookPos;
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

void CLazyClient::UpdateZeroGravityPredictor()
{
	UpdateZeroGravityWarningHideTimer();

	const bool ForcedMode = g_Config.m_LcZeroGravMode == 1;
	if(!g_Config.m_LcZeroGravPredict || Client()->State() != IClient::STATE_ONLINE ||
		(ForcedMode && !m_ZeroGravForcedEnabled))
	{
		ClearZeroGravityWarning(true);
		ResetZeroGravityTracking();
		return;
	}

	const CGameClient::CSnapState &Snap = GameClient()->m_Snap;
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

	const int Hook = pCurrentInput ? pCurrentInput->m_Hook : 1;
	const int Jump = pCurrentInput ? pCurrentInput->m_Jump : Core.m_Input.m_Jump;
	const float HitSeconds = SimulateZeroGravityFreeze(
		Core, Hook, HoldDirection, Jump, SnapshotHooked, vec2(pCharacter->m_HookX, pCharacter->m_HookY));

	if(HitSeconds < 0.0f)
	{
		ClearZeroGravityWarning(false);
		return;
	}

	m_ZeroGravWarning = true;
	m_ZeroGravSeconds = HitSeconds;
	m_ZeroGravWarningHideTimer = 0.0f;
}

void CLazyClient::RenderZeroGravityForcedStatus()
{
	if(g_Config.m_LcZeroGravMode != 1 ||
		Client()->LocalTime() >= m_ZeroGravForcedStatusUntil ||
		Client()->State() != IClient::STATE_ONLINE ||
		GameClient()->m_Menus.IsActive() || GameClient()->m_Scoreboard.IsActive())
		return;

	const CScreenRect OldScreen = Graphics()->GetScreen();
	const float ScaleX = std::clamp(g_Config.m_LcZeroGravScaleX, 50, 300) / 100.0f;
	const float ScaleY = std::clamp(g_Config.m_LcZeroGravScaleY, 50, 300) / 100.0f;
	const float Width = 300.0f * Graphics()->ScreenAspect() / ScaleX;
	const float Height = 300.0f / ScaleY;
	Graphics()->MapScreenToSize(Width, Height);
	const float OffsetX = g_Config.m_LcZeroGravOffsetX / ScaleX;
	const float OffsetY = g_Config.m_LcZeroGravOffsetY / ScaleY;

	const char *pStatus = m_ZeroGravForcedStatusOn ?
		Localize("Zero gravity predictor ON") : Localize("Zero gravity predictor OFF");
	const float FontSize = 10.5f;
	const float TextWidth = TextRender()->TextWidth(FontSize, pStatus);
	const float BoxWidth = std::max(TextWidth + 14.0f, 120.0f);
	CUIRect Box = {Width / 2.0f - BoxWidth / 2.0f + OffsetX, 32.0f / ScaleY + OffsetY, BoxWidth, 22.0f};
	const ColorRGBA Background = m_ZeroGravForcedStatusOn ?
		ColorRGBA(0.04f, 0.35f, 0.16f, 0.86f) : ColorRGBA(0.32f, 0.08f, 0.08f, 0.86f);
	const ColorRGBA Outline = m_ZeroGravForcedStatusOn ?
		ColorRGBA(0.45f, 1.0f, 0.65f, 0.9f) : ColorRGBA(1.0f, 0.55f, 0.45f, 0.9f);
	Box.Draw(Background, IGraphics::CORNER_ALL, 3.0f);
	Box.DrawOutline(Outline);

	TextRender()->TextColor(ColorRGBA(0.92f, 1.0f, 0.95f, 1.0f));
	Ui()->DoLabel(&Box, pStatus, FontSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	Graphics()->MapScreen(OldScreen);
}

void CLazyClient::RenderZeroGravityWarning()
{
	if(!g_Config.m_LcZeroGravPredict || !m_ZeroGravWarning ||
		Client()->State() != IClient::STATE_ONLINE ||
		GameClient()->m_Menus.IsActive() || GameClient()->m_Scoreboard.IsActive())
		return;

	const CScreenRect OldScreen = Graphics()->GetScreen();
	const float ScaleX = std::clamp(g_Config.m_LcZeroGravScaleX, 50, 300) / 100.0f;
	const float ScaleY = std::clamp(g_Config.m_LcZeroGravScaleY, 50, 300) / 100.0f;
	const float Width = 300.0f * Graphics()->ScreenAspect() / ScaleX;
	const float Height = 300.0f / ScaleY;
	Graphics()->MapScreenToSize(Width, Height);
	const float OffsetX = g_Config.m_LcZeroGravOffsetX / ScaleX;
	const float OffsetY = g_Config.m_LcZeroGravOffsetY / ScaleY;

	char aCountdown[160];
	const bool Freeview = GameClient()->m_Snap.m_SpecInfo.m_Active &&
		GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW &&
		m_ZeroGravTarget >= 0 && m_ZeroGravTarget < MAX_CLIENTS;
	if(g_Config.m_LcZeroGravText[0] != '\0')
	{
		if(Freeview)
			str_format(aCountdown, sizeof(aCountdown), "%s: %s %.1fs", GameClient()->m_aClients[m_ZeroGravTarget].m_aName, g_Config.m_LcZeroGravText, m_ZeroGravSeconds);
		else
			str_format(aCountdown, sizeof(aCountdown), "%s %.1fs", g_Config.m_LcZeroGravText, m_ZeroGravSeconds);
	}
	else if(Freeview)
		str_format(aCountdown, sizeof(aCountdown), Localize("%s: Freeze in %.1fs"), GameClient()->m_aClients[m_ZeroGravTarget].m_aName, m_ZeroGravSeconds);
	else
		str_format(aCountdown, sizeof(aCountdown), Localize("Freeze in %.1fs"), m_ZeroGravSeconds);

	const bool StatusVisible = g_Config.m_LcZeroGravMode == 1 &&
		Client()->LocalTime() < m_ZeroGravForcedStatusUntil;
	const float CountdownSize = 10.5f;
	const float CountdownWidth = TextRender()->TextWidth(CountdownSize, aCountdown);
	const float BoxWidth = std::max(CountdownWidth + 14.0f, 96.0f);
	CUIRect Box = {Width / 2.0f - BoxWidth / 2.0f + OffsetX, (StatusVisible ? 58.0f : 32.0f) / ScaleY + OffsetY, BoxWidth, 22.0f};
	Box.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_LcZeroGravBgColor, true)), IGraphics::CORNER_ALL, 3.0f);
	Box.DrawOutline(ColorRGBA(1.0f, 0.35f, 0.25f, 0.9f));

	TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_LcZeroGravTextColor, false)));
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
	const float ScaleX = std::clamp(g_Config.m_LcTelemetryScaleX, 50, 300) / 100.0f;
	const float ScaleY = std::clamp(g_Config.m_LcTelemetryScaleY, 50, 300) / 100.0f;
	const float Width = 300.0f * Graphics()->ScreenAspect() / ScaleX;
	const float Height = 300.0f / ScaleY;
	Graphics()->MapScreenToSize(Width, Height);
	const float OffsetX = g_Config.m_LcTelemetryOffsetX / ScaleX;
	const float OffsetY = g_Config.m_LcTelemetryOffsetY / ScaleY;

	const float FontSize = 9.5f;
	const float LineHeight = 11.0f;
	float MaxTextWidth = 0.0f;
	for(int i = 0; i < NumLines; i++)
		MaxTextWidth = std::max(MaxTextWidth, TextRender()->TextWidth(FontSize, aLines[i]));

	const float BoxWidth = MaxTextWidth + 12.0f;
	const float BoxHeight = NumLines * LineHeight + 8.0f;
	CUIRect Box = {Width - BoxWidth - 6.0f / ScaleX + OffsetX, 34.0f / ScaleY + OffsetY, BoxWidth, BoxHeight};
	Box.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_LcTelemetryBgColor, true)), IGraphics::CORNER_ALL, 3.0f);
	Box.DrawOutline(ColorRGBA(0.45f, 0.65f, 0.85f, 0.35f));

	TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_LcTelemetryTextColor, false)));
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
	RenderZeroGravityForcedStatus();
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
		m_ZeroGravForcedEnabled = false;
		m_ZeroGravForcedStatusUntil = 0.0f;
	}
	else if(ForcedZeroGravEnabled && !m_ZeroGravWasForced)
	{
		BindZeroGravKeyForced();
		m_ZeroGravWasForced = true;
		m_ZeroGravForcedEnabled = false;
		m_ZeroGravForcedStatusUntil = 0.0f;
		ClearZeroGravityWarning(true);
		ResetZeroGravityTracking();
	}
	else if(!ForcedZeroGravEnabled && m_ZeroGravWasForced)
	{
		RestoreZeroGravKey();
		m_ZeroGravWasForced = false;
		m_ZeroGravForcedEnabled = false;
		m_ZeroGravForcedStatusUntil = 0.0f;
		ClearZeroGravityWarning(true);
		ResetZeroGravityTracking();
	}

	if(m_ZeroGravForcedBindHeld && !AnyForcedZeroGravityBindKeyPressed())
		m_ZeroGravForcedBindHeld = false;

	UpdateClientLogoState();
	UpdateBestClientList();
	UpdateAutoMargin();
	UpdateAntiping();
	UpdateAspectConfirm();
	UpdateZoomKeyBinds();
	UpdateZeroGravityPredictor();
	UpdateAutoTeamLock();
	UpdateAutoKill();
	UpdateTelemetry();
}
