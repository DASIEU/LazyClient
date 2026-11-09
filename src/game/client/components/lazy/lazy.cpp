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
#include <engine/image.h>
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
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
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
// The developer icon is intentionally not configurable: only this player name
// receives it, so it cannot be discovered and equipped from the console.
constexpr const char *DEV_ICON_NAME = "DASI";
// The public list only needs to be refreshed when the set of players on the
// server changes, never continuously. Keep a hard floor between requests so a
// burst of joins cannot turn into a burst of downloads.
constexpr int64_t BESTCLIENT_LIST_MIN_INTERVAL_SECONDS = 30;
constexpr int64_t BESTCLIENT_LIST_MAX_RESPONSE_SIZE = 4 * 1024 * 1024;

bool CopyMessageBetween(const char *pMessage, const char *pPrefix, const char *pSuffix, char *pOut, int OutSize)
{
	if(pMessage == nullptr || pPrefix == nullptr || pSuffix == nullptr || OutSize <= 0)
		return false;
	const char *pStart = str_find(pMessage, pPrefix);
	if(pStart == nullptr)
		return false;
	pStart += str_length(pPrefix);
	const char *pEnd = str_find(pStart, pSuffix);
	if(pEnd == nullptr || pEnd <= pStart)
		return false;
	const int Length = (int)(pEnd - pStart);
	if(Length >= OutSize)
		return false;
	str_copy(pOut, pStart, Length + 1);
	return true;
}

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

bool IsGreetingMessage(const char *pMessage)
{
	const char *p = str_skip_whitespaces_const(pMessage);
	char aToken[32];
	size_t Length = 0;
	while(*p != '\0' && Length + 1 < sizeof(aToken))
	{
		char c = *p;
		if(c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
		if(!((c >= 'a' && c <= 'z') || c == '\''))
			break;
		aToken[Length++] = c;
		p++;
	}
	aToken[Length] = '\0';
	if(Length == 0)
		return false;

	static const char *const s_apGreetings[] = {
		"hi", "hii", "hiii", "hello", "helo", "hey", "heya", "yo", "qq", "qqq", "hai", "hola"};
	for(const char *pGreeting : s_apGreetings)
	{
		if(str_comp(aToken, pGreeting) == 0)
			return true;
	}
	return false;
}

enum EHudAlign
{
	HUD_ALIGN_LEFT = 0,
	HUD_ALIGN_CENTER,
	HUD_ALIGN_RIGHT,
	HUD_ALIGN_FRACTION,
	HUD_ALIGN_FULL,
};

struct SHudModuleDef
{
	const char *m_pName;
	int m_Align;
	float m_X;
	float m_Y;
	float m_W;
	float m_H;
};

// Approximate on-screen bounds in the standard 300-unit-high HUD space. The
// editor uses these as drag handles and outlines; the actual drawing is moved
// by translating the render target, so the bounds only need to be close.
const SHudModuleDef s_aHudModuleDefs[HUD_MODULE_COUNT] = {
	{"Health and ammo", HUD_ALIGN_LEFT, 0.0f, 0.0f, 62.0f, 52.0f},
	{"Player state", HUD_ALIGN_LEFT, 0.0f, 0.0f, 150.0f, 76.0f},
	{"Spectator count", HUD_ALIGN_RIGHT, 0.0f, 250.0f, 46.0f, 16.0f},
	{"Movement information", HUD_ALIGN_RIGHT, 0.0f, 222.0f, 72.0f, 62.0f},
	{"DDRace effects", HUD_ALIGN_CENTER, 0.0f, 22.0f, 160.0f, 24.0f},
	{"Spectating bar", HUD_ALIGN_RIGHT, 0.0f, 272.0f, 190.0f, 16.0f},
	{"Game timer", HUD_ALIGN_CENTER, 0.0f, 0.0f, 100.0f, 14.0f},
	{"Pause notification", HUD_ALIGN_CENTER, 0.0f, 48.0f, 150.0f, 26.0f},
	{"Sudden death", HUD_ALIGN_CENTER, 0.0f, 0.0f, 110.0f, 14.0f},
	{"Score HUD", HUD_ALIGN_RIGHT, 0.0f, 224.0f, 170.0f, 60.0f},
	{"Dummy actions", HUD_ALIGN_RIGHT, 0.0f, 218.0f, 20.0f, 32.0f},
	{"Warmup timer", HUD_ALIGN_CENTER, 0.0f, 46.0f, 130.0f, 30.0f},
	{"FPS and debug info", HUD_ALIGN_RIGHT, 0.0f, 0.0f, 90.0f, 44.0f},
	{"Local time", HUD_ALIGN_FRACTION, 0.42f, 0.0f, 34.0f, 14.0f},
	{"Connection warning", HUD_ALIGN_CENTER, 0.0f, 48.0f, 220.0f, 26.0f},
	{"Team balance warning", HUD_ALIGN_LEFT, 5.0f, 50.0f, 130.0f, 14.0f},
	{"Vote HUD", HUD_ALIGN_LEFT, 0.0f, 58.0f, 122.0f, 52.0f},
	{"Best times", HUD_ALIGN_LEFT, 0.0f, 72.0f, 110.0f, 22.0f},
	{"Center lines", HUD_ALIGN_CENTER, 0.0f, 138.0f, 48.0f, 24.0f},
	{"Status bar", HUD_ALIGN_FULL, 0.0f, -16.0f, 0.0f, 16.0f},
	{"Telemetry", HUD_ALIGN_RIGHT, 0.0f, 34.0f, 112.0f, 72.0f},
	{"Swap timers", HUD_ALIGN_LEFT, 0.0f, 34.0f, 150.0f, 90.0f},
	{"Zero-gravity warning", HUD_ALIGN_CENTER, 0.0f, 32.0f, 170.0f, 24.0f},
	{"Zero-gravity status", HUD_ALIGN_CENTER, 0.0f, 32.0f, 170.0f, 24.0f},
};

static_assert(sizeof(s_aHudModuleDefs) / sizeof(s_aHudModuleDefs[0]) == HUD_MODULE_COUNT, "HUD module table out of sync");

CUIRect HudModuleRect(const CLazyClient::SHudModuleState &State, const SHudModuleDef &Def, float Width, float Height)
{
	float X = 0.0f;
	switch(Def.m_Align)
	{
	case HUD_ALIGN_CENTER:
		X = Width / 2.0f - Def.m_W / 2.0f + Def.m_X;
		break;
	case HUD_ALIGN_RIGHT:
		X = Width - Def.m_W - Def.m_X;
		break;
	case HUD_ALIGN_FRACTION:
		X = Width * Def.m_X;
		break;
	case HUD_ALIGN_FULL:
		X = 0.0f;
		break;
	case HUD_ALIGN_LEFT:
	default:
		X = Def.m_X;
		break;
	}

	const float Y = Def.m_Y < 0.0f ? Height + Def.m_Y : Def.m_Y;
	const float W = Def.m_Align == HUD_ALIGN_FULL ? Width : Def.m_W;
	CUIRect Rect = {X + State.m_OffsetX, Y + State.m_OffsetY, W, Def.m_H};
	return Rect;
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
	Console()->Register("lc_hud_editor", "?i", CFGFLAG_CLIENT, ConHudEditor, this, "Open or close the HUD editor");
	Console()->Register("lc_hud_reset", "", CFGFLAG_CLIENT, ConHudReset, this, "Reset the HUD editor layout to the defaults");
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
	ResetSwapTimers();
	m_TeamLoadPending = false;
	m_TeamLoadCommands.clear();
	m_TeamLoadNextSend = 0;
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
	m_AutoGGArmed = false;
	m_aLastWhisperName[0] = '\0';
	m_AutoHiCooldowns.clear();
	if(NewState == IClient::STATE_ONLINE)
		m_BestClientListFetchPending = true;
	m_ZeroGravForcedEnabled = false;
	m_ZeroGravForcedStatusOn = false;
	m_ZeroGravForcedStatusUntil = 0.0f;
	ClearZeroGravityWarning(true);
	ResetZeroGravityTracking();
	ResetTelemetry();
	ResetAutoTeamLockState();
	ResetAutoKillState();
	ResetSwapTimers();
	m_TeamLoadPending = false;
	m_TeamLoadCommands.clear();
	m_TeamLoadNextSend = 0;
}

bool CLazyClient::UseDevIcon() const
{
	// Both the main and dummy connection can be the active local client, so
	// keep the icon when switching between them.
	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(LocalId >= 0 && LocalId < MAX_CLIENTS && GameClient()->m_aClients[LocalId].m_aName[0] != '\0' &&
			str_comp_nocase(GameClient()->m_aClients[LocalId].m_aName, DEV_ICON_NAME) == 0)
			return true;
	}
	const int ActiveId = GameClient()->m_Snap.m_LocalClientId;
	if(ActiveId >= 0 && ActiveId < MAX_CLIENTS && GameClient()->m_aClients[ActiveId].m_aName[0] != '\0' &&
		str_comp_nocase(GameClient()->m_aClients[ActiveId].m_aName, DEV_ICON_NAME) == 0)
		return true;
	return str_comp_nocase(g_Config.m_PlayerName, DEV_ICON_NAME) == 0;
}

void CLazyClient::ResetSwapTimers()
{
	m_SwapTimers.clear();
}

int CLazyClient::ClientIdByName(const char *pName) const
{
	if(pName == nullptr || pName[0] == '\0')
		return -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameClient()->m_aClients[i].m_aName[0] != '\0' &&
			str_comp_nocase(GameClient()->m_aClients[i].m_aName, pName) == 0)
			return i;
	}
	return -1;
}

bool CLazyClient::IsLocalName(const char *pName) const
{
	if(pName == nullptr || pName[0] == '\0')
		return false;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId >= 0 && LocalId < MAX_CLIENTS && GameClient()->m_aClients[LocalId].m_aName[0] != '\0')
		return str_comp_nocase(GameClient()->m_aClients[LocalId].m_aName, pName) == 0;
	return str_comp_nocase(g_Config.m_PlayerName, pName) == 0;
}

bool CLazyClient::IsNameInLocalTeam(const char *pName) const
{
	const int MyTeam = CurrentDDRaceTeam();
	if(MyTeam == TEAM_FLOCK)
		return false;
	const int ClientId = ClientIdByName(pName);
	if(ClientId < 0)
		return false;
	return GameClient()->m_Teams.Team(ClientId) == MyTeam;
}

void CLazyClient::AddOrRefreshSwapTimer(const char *pFrom, const char *pTo, int Seconds)
{
	if(pFrom == nullptr || pTo == nullptr || pFrom[0] == '\0' || pTo[0] == '\0')
		return;
	Seconds = std::clamp(Seconds, 1, 600);
	const int64_t Deadline = time() + (int64_t)Seconds;

	for(auto &Entry : m_SwapTimers)
	{
		const bool SameDirection = str_comp_nocase(Entry.m_aFrom, pFrom) == 0 &&
			str_comp_nocase(Entry.m_aTo, pTo) == 0;
		const bool ReverseDirection = str_comp_nocase(Entry.m_aFrom, pTo) == 0 &&
			str_comp_nocase(Entry.m_aTo, pFrom) == 0;
		if(!SameDirection && !ReverseDirection)
			continue;
		Entry.m_Deadline = Deadline;
		if(SameDirection)
		{
			str_copy(Entry.m_aFrom, pFrom, sizeof(Entry.m_aFrom));
			str_copy(Entry.m_aTo, pTo, sizeof(Entry.m_aTo));
		}
		return;
	}

	constexpr size_t MaxEntries = 12;
	if(m_SwapTimers.size() >= MaxEntries)
		m_SwapTimers.erase(m_SwapTimers.begin());

	SSwapTimerEntry Entry;
	str_copy(Entry.m_aFrom, pFrom, sizeof(Entry.m_aFrom));
	str_copy(Entry.m_aTo, pTo, sizeof(Entry.m_aTo));
	Entry.m_Deadline = Deadline;
	m_SwapTimers.push_back(Entry);
}

void CLazyClient::RemoveSwapTimer(const char *pA, const char *pB)
{
	if(pA == nullptr || pB == nullptr)
		return;
	for(auto It = m_SwapTimers.begin(); It != m_SwapTimers.end();)
	{
		const bool Same = (str_comp_nocase(It->m_aFrom, pA) == 0 && str_comp_nocase(It->m_aTo, pB) == 0) ||
			(str_comp_nocase(It->m_aFrom, pB) == 0 && str_comp_nocase(It->m_aTo, pA) == 0);
		if(Same)
			It = m_SwapTimers.erase(It);
		else
			++It;
	}
}

void CLazyClient::HandleSwapChatMessage(const char *pMessage)
{
	if(pMessage == nullptr)
		return;

	char aLocal[MAX_NAME_LENGTH];
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId >= 0 && LocalId < MAX_CLIENTS && GameClient()->m_aClients[LocalId].m_aName[0] != '\0')
		str_copy(aLocal, GameClient()->m_aClients[LocalId].m_aName, sizeof(aLocal));
	else
		str_copy(aLocal, g_Config.m_PlayerName, sizeof(aLocal));

	char aFirst[MAX_NAME_LENGTH];
	char aSecond[MAX_NAME_LENGTH];

	// "You have requested to swap with <name>. Use /cancelswap ..."
	// "You have already requested to swap with <name>."
	if(CopyMessageBetween(pMessage, "You have already requested to swap with ", ".", aSecond, sizeof(aSecond)) ||
		CopyMessageBetween(pMessage, "You have requested to swap with ", ". Use /cancelswap", aSecond, sizeof(aSecond)))
	{
		AddOrRefreshSwapTimer(aLocal, aSecond, 30);
		return;
	}

	// "<name> has requested to swap with you. ... wait <N> seconds ..."
	const char *pRequestedWithYou = str_find(pMessage, " has requested to swap with you.");
	if(pRequestedWithYou != nullptr)
	{
		const int Length = (int)(pRequestedWithYou - pMessage);
		if(Length > 0 && Length < (int)sizeof(aFirst))
		{
			str_copy(aFirst, pMessage, Length + 1);
			int Seconds = 30;
			const char *pWait = str_find(pMessage, "please wait ");
			if(pWait != nullptr)
				Seconds = str_toint(pWait + str_length("please wait "));
			AddOrRefreshSwapTimer(aFirst, aLocal, Seconds);
		}
		return;
	}

	// "<a> has requested to swap with <b>."
	const char *pRequested = str_find(pMessage, " has requested to swap with ");
	if(pRequested != nullptr)
	{
		const int FirstLength = (int)(pRequested - pMessage);
		const char *pTarget = pRequested + str_length(" has requested to swap with ");
		const char *pEnd = str_find(pTarget, ".");
		const int SecondLength = pEnd != nullptr ? (int)(pEnd - pTarget) : (int)str_length(pTarget);
		if(FirstLength > 0 && FirstLength < (int)sizeof(aFirst) &&
			SecondLength > 0 && SecondLength < (int)sizeof(aSecond))
		{
			str_copy(aFirst, pMessage, FirstLength + 1);
			str_copy(aSecond, pTarget, SecondLength + 1);
			AddOrRefreshSwapTimer(aFirst, aSecond, 30);
		}
		return;
	}

	// Cancellations, including the different "you" variants.
	if(CopyMessageBetween(pMessage, "You have canceled swap with ", ".", aSecond, sizeof(aSecond)) ||
		CopyMessageBetween(pMessage, "You have cancelled swap with ", ".", aSecond, sizeof(aSecond)))
	{
		RemoveSwapTimer(aLocal, aSecond);
		return;
	}
	const char *pCanceledWithYou = str_find(pMessage, " has canceled swap with you.");
	if(pCanceledWithYou == nullptr)
		pCanceledWithYou = str_find(pMessage, " has cancelled swap with you.");
	if(pCanceledWithYou != nullptr)
	{
		const int Length = (int)(pCanceledWithYou - pMessage);
		if(Length > 0 && Length < (int)sizeof(aFirst))
		{
			str_copy(aFirst, pMessage, Length + 1);
			RemoveSwapTimer(aFirst, aLocal);
		}
		return;
	}
	const char *pCanceledMarker = " has canceled swap with ";
	const char *pCanceled = str_find(pMessage, pCanceledMarker);
	if(pCanceled == nullptr)
	{
		pCanceledMarker = " has cancelled swap with ";
		pCanceled = str_find(pMessage, pCanceledMarker);
	}
	if(pCanceled != nullptr)
	{
		const int FirstLength = (int)(pCanceled - pMessage);
		const char *pTarget = pCanceled + str_length(pCanceledMarker);
		const char *pEnd = str_find(pTarget, ".");
		const int SecondLength = pEnd != nullptr ? (int)(pEnd - pTarget) : (int)str_length(pTarget);
		if(FirstLength > 0 && FirstLength < (int)sizeof(aFirst) &&
			SecondLength > 0 && SecondLength < (int)sizeof(aSecond))
		{
			str_copy(aFirst, pMessage, FirstLength + 1);
			str_copy(aSecond, pTarget, SecondLength + 1);
			RemoveSwapTimer(aFirst, aSecond);
		}
		return;
	}

	// "<a> has swapped with <b>."
	const char *pSwapped = str_find(pMessage, " has swapped with ");
	if(pSwapped != nullptr)
	{
		const int FirstLength = (int)(pSwapped - pMessage);
		const char *pTarget = pSwapped + str_length(" has swapped with ");
		const char *pEnd = str_find(pTarget, ".");
		const int SecondLength = pEnd != nullptr ? (int)(pEnd - pTarget) : (int)str_length(pTarget);
		if(FirstLength > 0 && FirstLength < (int)sizeof(aFirst) &&
			SecondLength > 0 && SecondLength < (int)sizeof(aSecond))
		{
			str_copy(aFirst, pMessage, FirstLength + 1);
			str_copy(aSecond, pTarget, SecondLength + 1);
			RemoveSwapTimer(aFirst, aSecond);
		}
		return;
	}

	// "Your swap request timed out ..." clears the local player's requests.
	if(str_find(pMessage, "Your swap request timed out") != nullptr)
	{
		for(auto It = m_SwapTimers.begin(); It != m_SwapTimers.end();)
		{
			if(IsLocalName(It->m_aFrom) || IsLocalName(It->m_aTo))
				It = m_SwapTimers.erase(It);
			else
				++It;
		}
	}
}

void CLazyClient::RenderSwapTimers()
{
	if(!g_Config.m_LcSwapTimer || Client()->State() != IClient::STATE_ONLINE ||
		GameClient()->m_Menus.IsActive())
		return;

	const int64_t Now = time();
	for(auto It = m_SwapTimers.begin(); It != m_SwapTimers.end();)
	{
		if(It->m_Deadline <= Now)
			It = m_SwapTimers.erase(It);
		else
			++It;
	}
	if(m_SwapTimers.empty())
		return;

	const int Mode = std::clamp(g_Config.m_LcSwapTimerMode, 0, 2);
	char aaRows[12][192];
	int RowSeconds[12] = {};
	int NumRows = 0;
	for(const auto &Entry : m_SwapTimers)
	{
		if(NumRows >= (int)(sizeof(aaRows) / sizeof(aaRows[0])))
			break;
		if(Mode == 2 && !IsLocalName(Entry.m_aFrom) && !IsLocalName(Entry.m_aTo))
			continue;
		if(Mode == 1 && !IsNameInLocalTeam(Entry.m_aFrom) && !IsNameInLocalTeam(Entry.m_aTo))
			continue;
		str_format(aaRows[NumRows], sizeof(aaRows[NumRows]), "%s ↔ %s", Entry.m_aFrom, Entry.m_aTo);
		RowSeconds[NumRows] = (int)std::max<int64_t>(0, Entry.m_Deadline - Now);
		NumRows++;
	}
	if(NumRows == 0)
		return;

	if(m_aHudModules[HUD_MODULE_SWAP_TIMERS].m_Hidden)
		return;

	const CScreenRect OldScreen = Graphics()->GetScreen();
	const float Width = 300.0f * Graphics()->ScreenAspect();
	const float Height = 300.0f;
	Graphics()->MapScreenToSize(Width, Height);

	const float FontSize = 9.5f;
	const float LineHeight = 11.0f;
	const char *pTitle = Localize("Swap requests");
	float MaxNameWidth = TextRender()->TextWidth(FontSize, pTitle);
	for(int i = 0; i < NumRows; i++)
		MaxNameWidth = std::max(MaxNameWidth, TextRender()->TextWidth(FontSize, aaRows[i]));
	const float SecondsWidth = TextRender()->TextWidth(FontSize, "999s");
	const float BoxWidth = MaxNameWidth + SecondsWidth + 20.0f;
	const float BoxHeight = (NumRows + 1) * LineHeight + 8.0f;
	CUIRect Box = {6.0f, 34.0f, BoxWidth, BoxHeight};
	Box.x += m_aHudModules[HUD_MODULE_SWAP_TIMERS].m_OffsetX;
	Box.y += m_aHudModules[HUD_MODULE_SWAP_TIMERS].m_OffsetY;
	m_aHudModuleRects[HUD_MODULE_SWAP_TIMERS] = Box;
	Box.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_LcTelemetryBgColor, true)), IGraphics::CORNER_ALL, 3.0f);

	TextRender()->TextColor(ColorRGBA(1.0f, 0.82f, 0.38f, 1.0f));
	TextRender()->Text(Box.x + 6.0f, Box.y + 4.0f, FontSize, pTitle);
	TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_LcTelemetryTextColor, true)));
	float y = Box.y + 4.0f + LineHeight;
	for(int i = 0; i < NumRows; i++)
	{
		TextRender()->Text(Box.x + 6.0f, y, FontSize, aaRows[i]);
		char aSeconds[16];
		str_format(aSeconds, sizeof(aSeconds), "%ds", RowSeconds[i]);
		const float SecondsX = Box.x + Box.w - 6.0f - TextRender()->TextWidth(FontSize, aSeconds);
		TextRender()->Text(SecondsX, y, FontSize, aSeconds);
		y += LineHeight;
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	Graphics()->MapScreen(OldScreen);
}

void CLazyClient::ApplyClientTypeToColor(int &ColorBody, int &ColorFeet) const
{
	if(!g_Config.m_LcClientLogos)
		return;
	ColorBody = LazyCCIDInsert(ColorBody, UseDevIcon() ? LC_CCID_BODY_LAZYCLIENT_DEV : LC_CCID_BODY_LAZYCLIENT);
	ColorFeet = LazyCCIDInsert(ColorFeet, LC_CCID_FEET_LAZYCLIENT);
}

int CLazyClient::ClientType(int ClientId) const
{
	if(!g_Config.m_LcClientLogos || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return LC_CLIENT_NONE;
	return m_aClientTypes[ClientId];
}

IGraphics::CTextureHandle CLazyClient::DevIconTexture() const
{
	if(m_DevIconTexture.IsValid())
		return m_DevIconTexture;

	// The developer icon is generated in code on purpose: it is not a data file,
	// so it cannot be swapped by dropping another image next to the other
	// client icons. It mirrors the normal LazyClient icon (same 128 px size and
	// rounded "Lz" lettering) with a black smoky gradient and gold letters.
	constexpr int Size = 128;
	constexpr int Samples = 4;
	constexpr float Radius = 27.0f;
	constexpr float Stroke = 4.5f;
	constexpr float OutlineWidth = 1.2f;

	auto Clamp01 = [](float Value) {
		return std::clamp(Value, 0.0f, 1.0f);
	};
	auto Smooth = [&](float Value) {
		Value = Clamp01(Value);
		return Value * Value * (3.0f - 2.0f * Value);
	};
	auto RoundedRectDistance = [&](float x, float y) {
		const float Qx = std::abs(x - Size * 0.5f) - (Size * 0.5f - Radius);
		const float Qy = std::abs(y - Size * 0.5f) - (Size * 0.5f - Radius);
		const float OutsideX = std::max(Qx, 0.0f);
		const float OutsideY = std::max(Qy, 0.0f);
		return std::sqrt(OutsideX * OutsideX + OutsideY * OutsideY) + std::min(std::max(Qx, Qy), 0.0f) - Radius;
	};
	auto SegmentDistance = [](float x, float y, float Ax, float Ay, float Bx, float By) {
		const float Pax = x - Ax;
		const float Pay = y - Ay;
		const float Bax = Bx - Ax;
		const float Bay = By - Ay;
		const float LengthSq = Bax * Bax + Bay * Bay;
		const float t = LengthSq > 0.0f ? std::clamp((Pax * Bax + Pay * Bay) / LengthSq, 0.0f, 1.0f) : 0.0f;
		const float Dx = Pax - Bax * t;
		const float Dy = Pay - Bay * t;
		return std::sqrt(Dx * Dx + Dy * Dy);
	};
	// Distance to the middle line of the rounded "Lz"; negative inside.
	// The geometry mirrors the normal LazyClient icon: a full-height L and a
	// smaller lower-case z sitting on the same baseline.
	auto LetterDistance = [&](float x, float y) {
		const float L = std::min(
			SegmentDistance(x, y, 40.0f, 44.0f, 40.0f, 78.0f),
			SegmentDistance(x, y, 40.0f, 78.0f, 62.0f, 78.0f));
		const float Z = std::min(
			SegmentDistance(x, y, 74.0f, 57.0f, 92.0f, 57.0f),
			std::min(
				SegmentDistance(x, y, 74.0f, 78.0f, 92.0f, 78.0f),
				SegmentDistance(x, y, 90.0f, 61.0f, 76.0f, 74.0f)));
		return std::min(L, Z) - Stroke;
	};

	CImageInfo Info;
	Info.m_Width = Size;
	Info.m_Height = Size;
	Info.m_Format = CImageInfo::FORMAT_RGBA;
	Info.AllocateFillZero();

	for(int y = 0; y < Size; y++)
	{
		for(int x = 0; x < Size; x++)
		{
			float Red = 0.0f;
			float Green = 0.0f;
			float Blue = 0.0f;
			int MaskSamples = 0;
			const int SampleCount = Samples * Samples;
			for(int sy = 0; sy < Samples; sy++)
			{
				for(int sx = 0; sx < Samples; sx++)
				{
					const float Px = x + (sx + 0.5f) / Samples;
					const float Py = y + (sy + 0.5f) / Samples;
					const float MaskDistance = RoundedRectDistance(Px, Py);
					if(MaskDistance > 0.0f)
						continue;
					MaskSamples++;

					// Black to smoky gray diagonal gradient with a soft
					// highlight, a very faint smoke ripple and a dark vignette.
					const float Gradient = Smooth((Px * 0.60f + Py * 0.50f) / Size);
					float R = 0.015f + (0.30f - 0.015f) * Gradient;
					float G = 0.015f + (0.30f - 0.015f) * Gradient;
					float B = 0.020f + (0.35f - 0.020f) * Gradient;
					const float GlowX = (Px - 36.0f) / 82.0f;
					const float GlowY = (Py - 28.0f) / 94.0f;
					const float Glow = Smooth(1.0f - std::sqrt(GlowX * GlowX + GlowY * GlowY));
					R += Glow * 0.060f;
					G += Glow * 0.060f;
					B += Glow * 0.068f;
					const float Smoke = std::sin(Px * 0.055f + std::sin(Py * 0.090f) * 2.1f);
					R += Smoke * 0.014f;
					G += Smoke * 0.014f;
					B += Smoke * 0.017f;
					const float Vx = (Px - Size * 0.5f) / (Size * 0.5f);
					const float Vy = (Py - Size * 0.5f) / (Size * 0.5f);
					const float Vignette = 0.80f + 0.20f * Smooth(1.0f - std::sqrt(Vx * Vx + Vy * Vy));
					R *= Vignette;
					G *= Vignette;
					B *= Vignette;

					// A light inner rim keeps the rounded edge readable
					// against dark maps.
					if(MaskDistance > -1.7f)
					{
						const float Rim = 0.22f;
						R += (0.62f - R) * Rim;
						G += (0.62f - G) * Rim;
						B += (0.68f - B) * Rim;
					}

					const float Letter = LetterDistance(Px, Py);
					const float Shadow = LetterDistance(Px - 1.8f, Py - 2.4f);
					if(Shadow <= 0.0f)
					{
						R *= 0.42f;
						G *= 0.42f;
						B *= 0.42f;
					}

					if(Letter <= OutlineWidth)
					{
						R = 0.30f;
						G = 0.17f;
						B = 0.025f;
					}
					if(Letter <= 0.0f)
					{
						// Gold gradient from pale top to deep amber bottom.
						const float GoldT = Clamp01((Py - 35.0f) / 58.0f);
						float GoldR, GoldG, GoldB;
						if(GoldT < 0.55f)
						{
							const float t = GoldT / 0.55f;
							GoldR = 1.0f + (1.0f - 1.0f) * t;
							GoldG = 0.95f + (0.78f - 0.95f) * t;
							GoldB = 0.66f + (0.22f - 0.66f) * t;
						}
						else
						{
							const float t = (GoldT - 0.55f) / 0.45f;
							GoldR = 1.0f + (0.70f - 1.0f) * t;
							GoldG = 0.78f + (0.42f - 0.78f) * t;
							GoldB = 0.22f + (0.035f - 0.22f) * t;
						}
						const float Sheen = 0.10f * (1.0f - Clamp01((Px - 28.0f) / 74.0f));
						R = GoldR + Sheen;
						G = GoldG + Sheen;
						B = GoldB + Sheen * 0.7f;
					}

					Red += R;
					Green += G;
					Blue += B;
				}
			}
			if(MaskSamples == 0)
				continue;

			uint8_t *pPixel = &Info.m_pData[(y * Size + x) * 4];
			pPixel[0] = (uint8_t)(std::clamp(Red / MaskSamples, 0.0f, 1.0f) * 255.0f);
			pPixel[1] = (uint8_t)(std::clamp(Green / MaskSamples, 0.0f, 1.0f) * 255.0f);
			pPixel[2] = (uint8_t)(std::clamp(Blue / MaskSamples, 0.0f, 1.0f) * 255.0f);
			pPixel[3] = (uint8_t)(255 * MaskSamples / SampleCount);
		}
	}

	m_DevIconTexture = Graphics()->LoadTextureRaw(Info, 0, "lazyclient_dev");
	return m_DevIconTexture;
}

IGraphics::CTextureHandle CLazyClient::ClientIcon(int Type) const
{
	switch(Type)
	{
	case LC_CLIENT_LAZYCLIENT:
		return g_pData->m_aImages[IMAGE_LAZYCLIENT].m_Id;
	case LC_CLIENT_LAZYCLIENT_DEV:
		return DevIconTexture();
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
			case LC_CCID_BODY_LAZYCLIENT_DEV:
				Type = LC_CLIENT_LAZYCLIENT_DEV;
				break;
			}
		}

		if(Type == LC_CLIENT_NONE && pBestClientNames != nullptr && Client.m_aName[0] != '\0' &&
			pBestClientNames->find(Client.m_aName) != pBestClientNames->end())
		{
			Type = LC_CLIENT_BESTCLIENT;
		}

		// Hardcoded developer tag: every LazyClient sees the dev icon for this
		// name, even if the player's color still carries the normal LazyClient id.
		if(Client.m_aName[0] != '\0' && str_comp_nocase(Client.m_aName, DEV_ICON_NAME) == 0)
		{
			Type = LC_CLIENT_LAZYCLIENT_DEV;
		}

		if(State == IClient::STATE_ONLINE &&
			(ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1]))
		{
			// The server may not have echoed our custom color yet, and 0.7
			// servers do not use the skin color convention.
			Type = UseDevIcon() ? LC_CLIENT_LAZYCLIENT_DEV : LC_CLIENT_LAZYCLIENT;
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
	m_BestClientListFetchPending = false;
	m_BestClientListLoaded = false;
	m_BestClientListPlayerCount = -1;
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
	if(Wanted && !m_BestClientListWanted)
		m_BestClientListFetchPending = true;
	m_BestClientListWanted = Wanted;
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
		m_BestClientListNextFetch = 0;
		m_BestClientListFetchPending = false;
		m_BestClientListPlayerCount = -1;
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
			// Keep the request pending so a later join can retry, but respect
			// the same minimum interval as a successful download.
			m_BestClientListFetchPending = true;
		}
		else
		{
			return;
		}
	}

	if(!m_BestClientListFetchPending)
	{
		// Initial fetch or a reset while already online: there is no join event
		// to wait for in that case.
		if(!m_BestClientListLoaded && !m_pBestClientListTask)
			m_BestClientListFetchPending = true;
		else
			return;
	}

	const int64_t Now = time();
	if(Now < m_BestClientListNextFetch)
		return;

	m_BestClientListFetchPending = false;
	m_BestClientListNextFetch = Now + BESTCLIENT_LIST_MIN_INTERVAL_SECONDS * time_freq();

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
	// Use the explicit argument: plain /lock toggles, so sending it to an
	// already-locked team would unlock it instead.
	GameClient()->m_Chat.SendChat(0, "/lock 1");
}

void CLazyClient::ResetAutoTeamLockState()
{
	m_AutoTeamLockTeam = -1;
	m_AutoTeamLockPending = false;
	m_AutoTeamLockDeadline = 0;
	m_AutoTeamLockLocked = false;
}

void CLazyClient::SaveTeamNow()
{
	const int Team = CurrentDDRaceTeam();
	if(Team == TEAM_FLOCK)
	{
		GameClient()->m_Chat.Echo(Localize("LazyClient: join a team before saving it."));
		return;
	}

	m_TeamLoadPending = false;
	m_TeamLoadCommands.clear();
	m_TeamSaveTeam = Team;
	m_NumTeamSaveNames = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[ClientId])
			continue;
		if(GameClient()->m_Teams.Team(ClientId) != Team)
			continue;
		const char *pName = GameClient()->m_aClients[ClientId].m_aName;
		if(pName[0] == '\0')
			continue;
		str_copy(m_aaTeamSaveNames[m_NumTeamSaveNames], pName, MAX_NAME_LENGTH);
		m_NumTeamSaveNames++;
		if(m_NumTeamSaveNames >= MAX_CLIENTS)
			break;
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), Localize("LazyClient: saved team %d with %d players. Use /lt to load it."), Team, m_NumTeamSaveNames);
	GameClient()->m_Chat.Echo(aBuf);
}

void CLazyClient::StartTeamLoad()
{
	if(m_TeamSaveTeam <= TEAM_FLOCK || m_NumTeamSaveNames == 0)
	{
		GameClient()->m_Chat.Echo(Localize("LazyClient: no saved team. Use /st while in a team first."));
		return;
	}

	m_TeamLoadCommands.clear();
	m_TeamLoadPending = false;

	const bool InTeam = CurrentDDRaceTeam() != TEAM_FLOCK;
	const bool Playing = GameClient()->m_Snap.m_pLocalCharacter != nullptr && !GameClient()->m_Snap.m_SpecInfo.m_Active;
	if(InTeam || Playing)
	{
		m_TeamLoadPending = true;
		m_TeamLoadConfirmUntil = time() + TEAM_LOAD_CONFIRM_SECONDS * time_freq();
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), Localize("LazyClient: loading team %d will restart your run. Type yes to kill and load, or no to cancel."), m_TeamSaveTeam);
		GameClient()->m_Chat.Echo(aBuf);
		return;
	}

	RunTeamLoad(false);
}

void CLazyClient::RunTeamLoad(bool Kill)
{
	m_TeamLoadPending = false;
	m_TeamLoadCommands.clear();

	if(Kill)
		QueueTeamLoadCommand("/kill");

	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "/team %d", m_TeamSaveTeam);
	QueueTeamLoadCommand(aBuf);

	const int LocalId = GameClient()->m_aLocalIds[0];
	const char *pLocalName = LocalId >= 0 && LocalId < MAX_CLIENTS ? GameClient()->m_aClients[LocalId].m_aName : "";
	for(int i = 0; i < m_NumTeamSaveNames; i++)
	{
		const char *pName = m_aaTeamSaveNames[i];
		if(pLocalName[0] != '\0' && str_comp_nocase(pName, pLocalName) == 0)
			continue;
		std::string Command = "/invite ";
		Command += pName;
		QueueTeamLoadCommand(Command.c_str());
	}

	m_TeamLoadNextSend = time();
	str_format(aBuf, sizeof(aBuf), Localize("LazyClient: loading team %d..."), m_TeamSaveTeam);
	GameClient()->m_Chat.Echo(aBuf);
}

void CLazyClient::QueueTeamLoadCommand(const char *pCommand)
{
	m_TeamLoadCommands.emplace_back(pCommand);
}

void CLazyClient::UpdateTeamLoadQueue()
{
	if(m_TeamLoadPending && time() > m_TeamLoadConfirmUntil)
	{
		m_TeamLoadPending = false;
		GameClient()->m_Chat.Echo(Localize("LazyClient: load cancelled (timed out)."));
	}

	if(m_TeamLoadCommands.empty())
		return;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		m_TeamLoadCommands.clear();
		return;
	}

	const int64_t Now = time();
	if(Now < m_TeamLoadNextSend)
		return;

	const std::string Command = m_TeamLoadCommands.front();
	m_TeamLoadCommands.erase(m_TeamLoadCommands.begin());
	GameClient()->m_Chat.SendChat(0, Command.c_str());
	// The server rate-limits invites (sv_invite_frequency, 1 s by default), so
	// leave a little headroom between the queued commands.
	m_TeamLoadNextSend = Now + time_freq() + time_freq() / 10;
}

bool CLazyClient::ChatDoTeamSave(const char *pInput)
{
	if(pInput == nullptr)
		return false;

	char aLine[256];
	str_copy(aLine, pInput, sizeof(aLine));
	str_utf8_trim_right(aLine);
	const char *pLine = str_skip_whitespaces_const(aLine);

	if(m_TeamLoadPending)
	{
		if(time() > m_TeamLoadConfirmUntil)
		{
			m_TeamLoadPending = false;
			GameClient()->m_Chat.Echo(Localize("LazyClient: load cancelled (timed out)."));
		}
		else if(str_comp_nocase(pLine, "yes") == 0)
		{
			RunTeamLoad(true);
			return true;
		}
		else if(str_comp_nocase(pLine, "no") == 0)
		{
			m_TeamLoadPending = false;
			GameClient()->m_Chat.Echo(Localize("LazyClient: load cancelled."));
			return true;
		}
	}

	char aCommand[32];
	if(sscanf(pLine, "%31s", aCommand) != 1)
		return false;

	if(str_comp_nocase(aCommand, "/st") == 0 || str_comp_nocase(aCommand, "/saveteam") == 0)
	{
		SaveTeamNow();
		return true;
	}
	if(str_comp_nocase(aCommand, "/lt") == 0 || str_comp_nocase(aCommand, "/loadteam") == 0)
	{
		StartTeamLoad();
		return true;
	}
	return false;
}

bool CLazyClient::ChatDoReply(const char *pInput)
{
	if(pInput == nullptr)
		return false;

	char aLine[256];
	str_copy(aLine, pInput, sizeof(aLine));
	str_utf8_trim_right(aLine);
	const char *pLine = str_skip_whitespaces_const(aLine);

	if(str_comp_nocase_num(pLine, "/r", 2) != 0 || (pLine[2] != '\0' && pLine[2] != ' '))
		return false;

	const char *pMessage = str_skip_whitespaces_const(pLine + 2);
	if(m_aLastWhisperName[0] == '\0')
	{
		GameClient()->m_Chat.Echo(Localize("LazyClient: nobody has whispered you yet."));
		return true;
	}
	if(*pMessage == '\0')
	{
		GameClient()->m_Chat.Echo(Localize("LazyClient: usage: /r <message>"));
		return true;
	}

	char aBuf[MAX_CHAT_LENGTH + MAX_NAME_LENGTH + 8];
	str_format(aBuf, sizeof(aBuf), "/w %s %s", m_aLastWhisperName, pMessage);
	GameClient()->m_Chat.SendChat(0, aBuf);
	return true;
}

void CLazyClient::HandleAutoHi(int ClientId, int Team, const char *pMessage)
{
	if(!g_Config.m_LcAutoHi || pMessage == nullptr)
		return;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	if(ClientId == GameClient()->m_Snap.m_LocalClientId)
		return;
	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(LocalId >= 0 && ClientId == LocalId)
			return;
	}

	const CGameClient::CClientData &Client = GameClient()->m_aClients[ClientId];
	if(!Client.m_Active || Client.m_aName[0] == '\0' || Client.m_ChatIgnore || Client.m_Foe)
		return;
	if(!IsGreetingMessage(pMessage))
		return;

	auto Cooldown = m_AutoHiCooldowns.find(Client.m_aName);
	const int64_t Now = time();
	if(Cooldown != m_AutoHiCooldowns.end() && Now < Cooldown->second + 60 * time_freq())
		return;
	m_AutoHiCooldowns[Client.m_aName] = Now;

	std::vector<std::string> vReplies;
	const char *pCursor = g_Config.m_LcAutoHiMessages;
	char aToken[128];
	while((pCursor = str_next_token(pCursor, "|", aToken, sizeof(aToken))) != nullptr)
	{
		char *pReply = str_skip_whitespaces(aToken);
		str_utf8_trim_right(pReply);
		if(*pReply != '\0')
			vReplies.emplace_back(pReply);
	}
	if(vReplies.empty())
		return;

	const std::string &Reply = vReplies[rand() % vReplies.size()];
	char aBuf[MAX_CHAT_LENGTH + MAX_NAME_LENGTH + 8];
	if(g_Config.m_LcAutoHiMode == 2 || Team == TEAM_WHISPER_RECV)
	{
		// Never answer a whisper in public chat.
		str_format(aBuf, sizeof(aBuf), "/w %s %s", Client.m_aName, Reply.c_str());
		GameClient()->m_Chat.SendChat(0, aBuf);
	}
	else if(g_Config.m_LcAutoHiMode == 1)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %s", Client.m_aName, Reply.c_str());
		GameClient()->m_Chat.SendChat(Team == 1 ? 1 : 0, aBuf);
	}
	else
	{
		GameClient()->m_Chat.SendChat(Team == 1 ? 1 : 0, Reply.c_str());
	}
}

void CLazyClient::UpdateAutoGG()
{
	if(!g_Config.m_LcAutoGG || GameClient()->m_Snap.m_pLocalInfo == nullptr)
		return;

	if(GameClient()->m_Snap.m_pLocalInfo->m_Score == FinishTime::NOT_FINISHED_TIMESCORE)
	{
		// The team is still running; a time can arrive later.
		m_AutoGGArmed = true;
		return;
	}

	if(!m_AutoGGArmed)
		return;
	m_AutoGGArmed = false;

	if(g_Config.m_LcAutoGGMessage[0] == '\0')
		return;
	GameClient()->m_Chat.SendChat(g_Config.m_LcAutoGGChat ? 0 : 1, g_Config.m_LcAutoGGMessage);
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
		if(ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_aClients[ClientId].m_aName[0] != '\0')
			str_copy(m_aLastWhisperName, GameClient()->m_aClients[ClientId].m_aName, sizeof(m_aLastWhisperName));
		if(g_Config.m_LcInviteManager)
			HandleTeamInvite(ClientId, pMessage);
		HandleAutoHi(ClientId, Team, pMessage);
		return;
	}

	if(Team <= 1)
		HandleAutoHi(ClientId, Team, pMessage);

	// The client cannot read the server's lock flag, but DDNet announces every
	// lock and unlock change with a server message in the team chat:
	//   "'%s' locked your team. ..." / "'%s' unlocked your team."
	// SendChatTarget marks these as team 0 (all) even though only the team
	// receives them, so only the server sender matters here. Requiring a server
	// message also stops players from faking the state in their own chat.
	if(ClientId != SERVER_CHAT_CLIENT_ID)
		return;

	HandleSwapChatMessage(pMessage);

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
		m_AutoTeamLockDeadline = time() + (int64_t)std::clamp(g_Config.m_LcAutoTeamLockDelay, 1, 60) * time_freq();
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
	if(Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalInfo)
		return; // Transient snapshot gaps must not clear the lock state.

	const bool Enabled = g_Config.m_LcAutoTeamLock != 0;
	const int Team = CurrentDDRaceTeam();

	// A team change also covers leaving the team, being kicked from it, or the
	// team disbanding (all of which put us back in TEAM_FLOCK). Only then is the
	// remembered locked state discarded.
	if(Team != m_AutoTeamLockTeam)
	{
		m_AutoTeamLockTeam = Team;
		m_AutoTeamLockLocked = false;
		m_AutoTeamLockPending = false;
		m_AutoTeamLockDeadline = 0;

		if(Enabled && Team != TEAM_FLOCK)
		{
			m_AutoTeamLockPending = true;
			m_AutoTeamLockDeadline = time() + (int64_t)std::clamp(g_Config.m_LcAutoTeamLockDelay, 1, 60) * time_freq();
		}
		return;
	}

	if(!Enabled || Team == TEAM_FLOCK)
	{
		// Do not forget that the team is locked just because the option was
		// toggled off; otherwise turning it back on would lock it again.
		m_AutoTeamLockPending = false;
		m_AutoTeamLockDeadline = 0;
		return;
	}

	// Re-arm after toggling the option back on while the team is still unlocked.
	if(!m_AutoTeamLockPending && !m_AutoTeamLockLocked)
	{
		m_AutoTeamLockPending = true;
		m_AutoTeamLockDeadline = time() + (int64_t)std::clamp(g_Config.m_LcAutoTeamLockDelay, 1, 60) * time_freq();
	}

	if(!m_AutoTeamLockPending || m_AutoTeamLockLocked)
		return;

	// Use wall-clock time instead of accumulated render-frame time: rendering
	// and updates are not locked 1:1, so frame time could shorten the countdown.
	if(time() < m_AutoTeamLockDeadline)
		return;

	m_AutoTeamLockPending = false;
	m_AutoTeamLockLocked = true;
	// Use the explicit argument: plain /lock toggles, so sending it to an
	// already-locked team would unlock it instead.
	GameClient()->m_Chat.SendChat(0, "/lock 1");
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

	// The BestClient public list is only relevant when unknown players join.
	// Refresh it on joins instead of polling it every few frames.
	if(Client()->State() == IClient::STATE_ONLINE)
	{
		const int NumPlayers = GameClient()->m_Snap.m_NumPlayers;
		if(m_BestClientListPlayerCount >= 0 && NumPlayers > m_BestClientListPlayerCount)
			m_BestClientListFetchPending = true;
		m_BestClientListPlayerCount = NumPlayers;
	}

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

	UpdateAutoGG();

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
		GameClient()->m_Menus.IsActive() || GameClient()->m_Scoreboard.IsActive() ||
		m_aHudModules[HUD_MODULE_ZERO_GRAV_STATUS].m_Hidden)
		return;

	const CScreenRect OldScreen = Graphics()->GetScreen();
	const float ScaleX = std::clamp(g_Config.m_LcZeroGravScaleX, 50, 300) / 100.0f;
	const float ScaleY = std::clamp(g_Config.m_LcZeroGravScaleY, 50, 300) / 100.0f;
	const float Width = 300.0f * Graphics()->ScreenAspect() / ScaleX;
	const float Height = 300.0f / ScaleY;
	Graphics()->MapScreenToSize(Width, Height);
	const float OffsetX = g_Config.m_LcZeroGravOffsetX / ScaleX + m_aHudModules[HUD_MODULE_ZERO_GRAV_STATUS].m_OffsetX / ScaleX;
	const float OffsetY = g_Config.m_LcZeroGravOffsetY / ScaleY + m_aHudModules[HUD_MODULE_ZERO_GRAV_STATUS].m_OffsetY / ScaleY;

	const char *pStatus = m_ZeroGravForcedStatusOn ?
		Localize("Zero gravity predictor ON") : Localize("Zero gravity predictor OFF");
	const float FontSize = 10.5f;
	const float TextWidth = TextRender()->TextWidth(FontSize, pStatus);
	const float BoxWidth = std::max(TextWidth + 14.0f, 120.0f);
	CUIRect Box = {Width / 2.0f - BoxWidth / 2.0f + OffsetX, 32.0f / ScaleY + OffsetY, BoxWidth, 22.0f};
	m_aHudModuleRects[HUD_MODULE_ZERO_GRAV_STATUS] = {Box.x * ScaleX, Box.y * ScaleY, Box.w * ScaleX, Box.h * ScaleY};
	const ColorRGBA Background = m_ZeroGravForcedStatusOn ?
		ColorRGBA(0.04f, 0.35f, 0.16f, 0.86f) : ColorRGBA(0.32f, 0.08f, 0.08f, 0.86f);
	Box.Draw(Background, IGraphics::CORNER_ALL, 3.0f);

	TextRender()->TextColor(ColorRGBA(0.92f, 1.0f, 0.95f, 1.0f));
	Ui()->DoLabel(&Box, pStatus, FontSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	Graphics()->MapScreen(OldScreen);
}

void CLazyClient::RenderZeroGravityWarning()
{
	if(!g_Config.m_LcZeroGravPredict || !m_ZeroGravWarning ||
		Client()->State() != IClient::STATE_ONLINE ||
		GameClient()->m_Menus.IsActive() || GameClient()->m_Scoreboard.IsActive() ||
		m_aHudModules[HUD_MODULE_ZERO_GRAV_WARNING].m_Hidden)
		return;

	const CScreenRect OldScreen = Graphics()->GetScreen();
	const float ScaleX = std::clamp(g_Config.m_LcZeroGravScaleX, 50, 300) / 100.0f;
	const float ScaleY = std::clamp(g_Config.m_LcZeroGravScaleY, 50, 300) / 100.0f;
	const float Width = 300.0f * Graphics()->ScreenAspect() / ScaleX;
	const float Height = 300.0f / ScaleY;
	Graphics()->MapScreenToSize(Width, Height);
	const float OffsetX = g_Config.m_LcZeroGravOffsetX / ScaleX + m_aHudModules[HUD_MODULE_ZERO_GRAV_WARNING].m_OffsetX / ScaleX;
	const float OffsetY = g_Config.m_LcZeroGravOffsetY / ScaleY + m_aHudModules[HUD_MODULE_ZERO_GRAV_WARNING].m_OffsetY / ScaleY;

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
	m_aHudModuleRects[HUD_MODULE_ZERO_GRAV_WARNING] = {Box.x * ScaleX, Box.y * ScaleY, Box.w * ScaleX, Box.h * ScaleY};
	Box.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_LcZeroGravBgColor, true)), IGraphics::CORNER_ALL, 3.0f);

	TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_LcZeroGravTextColor, true)));
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

	if(m_aHudModules[HUD_MODULE_TELEMETRY].m_Hidden)
		return;

	const CScreenRect OldScreen = Graphics()->GetScreen();
	const float ScaleX = std::clamp(g_Config.m_LcTelemetryScaleX, 50, 300) / 100.0f;
	const float ScaleY = std::clamp(g_Config.m_LcTelemetryScaleY, 50, 300) / 100.0f;
	const float Width = 300.0f * Graphics()->ScreenAspect() / ScaleX;
	const float Height = 300.0f / ScaleY;
	Graphics()->MapScreenToSize(Width, Height);
	const float OffsetX = g_Config.m_LcTelemetryOffsetX / ScaleX + m_aHudModules[HUD_MODULE_TELEMETRY].m_OffsetX / ScaleX;
	const float OffsetY = g_Config.m_LcTelemetryOffsetY / ScaleY + m_aHudModules[HUD_MODULE_TELEMETRY].m_OffsetY / ScaleY;

	const float FontSize = 9.5f;
	const float LineHeight = 11.0f;
	float MaxTextWidth = 0.0f;
	for(int i = 0; i < NumLines; i++)
		MaxTextWidth = std::max(MaxTextWidth, TextRender()->TextWidth(FontSize, aLines[i]));

	const float BoxWidth = MaxTextWidth + 12.0f;
	const float BoxHeight = NumLines * LineHeight + 8.0f;
	CUIRect Box = {Width - BoxWidth - 6.0f / ScaleX + OffsetX, 34.0f / ScaleY + OffsetY, BoxWidth, BoxHeight};
	m_aHudModuleRects[HUD_MODULE_TELEMETRY] = {Box.x * ScaleX, Box.y * ScaleY, Box.w * ScaleX, Box.h * ScaleY};
	Box.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_LcTelemetryBgColor, true)), IGraphics::CORNER_ALL, 3.0f);

	TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_LcTelemetryTextColor, true)));
	float y = Box.y + 4.0f;
	for(int i = 0; i < NumLines; i++)
	{
		TextRender()->Text(Box.x + 6.0f, y, FontSize, aLines[i]);
		y += LineHeight;
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	Graphics()->MapScreen(OldScreen);
}

void CLazyClient::LoadHudLayout()
{
	for(auto &Module : m_aHudModules)
		Module = SHudModuleState();

	const char *pEntry = g_Config.m_LcHudLayout;
	while(pEntry != nullptr && *pEntry != '\0')
	{
		int ModuleId = -1;
		int OffsetX = 0;
		int OffsetY = 0;
		int Hidden = 0;
		if(sscanf(pEntry, "%d:%d:%d:%d", &ModuleId, &OffsetX, &OffsetY, &Hidden) == 4 &&
			ModuleId >= 0 && ModuleId < HUD_MODULE_COUNT)
		{
			m_aHudModules[ModuleId].m_OffsetX = std::clamp(OffsetX, -400, 400);
			m_aHudModules[ModuleId].m_OffsetY = std::clamp(OffsetY, -300, 300);
			m_aHudModules[ModuleId].m_Hidden = Hidden != 0;
		}
		pEntry = str_find(pEntry, ";");
		if(pEntry != nullptr)
			pEntry++;
	}

	// Old configs stored Lazy text colors without an alpha byte. Those values
	// would become fully transparent now that the editor exposes text alpha.
	if((g_Config.m_LcTelemetryTextColor >> 24) == 0)
		g_Config.m_LcTelemetryTextColor |= 0xFF000000;
	if((g_Config.m_LcZeroGravTextColor >> 24) == 0)
		g_Config.m_LcZeroGravTextColor |= 0xFF000000;
	m_HudLayoutLoaded = true;
}

void CLazyClient::SaveHudLayout()
{
	char aBuffer[1024] = "";
	for(int i = 0; i < HUD_MODULE_COUNT; i++)
	{
		const SHudModuleState &Module = m_aHudModules[i];
		if(Module.m_OffsetX == 0 && Module.m_OffsetY == 0 && !Module.m_Hidden)
			continue;
		char aEntry[64];
		str_format(aEntry, sizeof(aEntry), "%d:%d:%d:%d;", i, Module.m_OffsetX, Module.m_OffsetY, Module.m_Hidden ? 1 : 0);
		str_append(aBuffer, aEntry, sizeof(aBuffer));
	}
	str_copy(g_Config.m_LcHudLayout, aBuffer, sizeof(g_Config.m_LcHudLayout));
	ConfigManager()->Save();
}

void CLazyClient::ResetHudLayout()
{
	for(auto &Module : m_aHudModules)
		Module = SHudModuleState();
	g_Config.m_LcHudLayout[0] = '\0';
	m_HudLayoutLoaded = true;
	if(m_HudEditorActive)
		memcpy(m_aHudEditorBackup, m_aHudModules, sizeof(m_aHudModules));
	ConfigManager()->Save();
}

void CLazyClient::OpenHudEditor()
{
	if(!m_HudLayoutLoaded)
		LoadHudLayout();
	memcpy(m_aHudEditorBackup, m_aHudModules, sizeof(m_aHudModules));
	unsigned int *const apColors[6] = {
		&g_Config.m_LcTelemetryTextColor,
		&g_Config.m_LcTelemetryBgColor,
		&g_Config.m_LcZeroGravTextColor,
		&g_Config.m_LcZeroGravBgColor,
		&g_Config.m_TcStatusBarTextColor,
		&g_Config.m_TcStatusBarColor,
	};
	for(int i = 0; i < 6; i++)
		m_aHudEditorColorBackup[i] = *apColors[i];
	m_HudEditorMenuWasActive = GameClient()->m_Menus.IsActive();
	m_HudEditorActive = true;
	m_HudEditorDragging = -1;
	m_HudEditorSettings = -1;
	m_HudEditorConfirm = false;

	// The editor owns the cursor, so release it from the game's relative mouse
	// mode and keep the menu from fighting the editor for input.
	Input()->MouseModeAbsolute();
	Ui()->ClosePopupMenus();
	GameClient()->m_Menus.SetActive(false);
}

void CLazyClient::CloseHudEditor(bool Save)
{
	if(!m_HudEditorActive)
		return;
	if(Save)
		SaveHudLayout();
	else
	{
		memcpy(m_aHudModules, m_aHudEditorBackup, sizeof(m_aHudModules));
		unsigned int *const apColors[6] = {
			&g_Config.m_LcTelemetryTextColor,
			&g_Config.m_LcTelemetryBgColor,
			&g_Config.m_LcZeroGravTextColor,
			&g_Config.m_LcZeroGravBgColor,
			&g_Config.m_TcStatusBarTextColor,
			&g_Config.m_TcStatusBarColor,
		};
		for(int i = 0; i < 6; i++)
			*apColors[i] = m_aHudEditorColorBackup[i];
	}
	m_HudEditorActive = false;
	m_HudEditorDragging = -1;
	m_HudEditorSettings = -1;
	m_HudEditorConfirm = false;

	// Go back to the menu if that is where the editor was opened from, otherwise
	// resume the game with a grabbed cursor.
	if(m_HudEditorMenuWasActive || Client()->State() == IClient::STATE_OFFLINE || Client()->State() == IClient::STATE_LOADING)
		GameClient()->m_Menus.SetActive(true);
	m_HudEditorMenuWasActive = false;

	Ui()->ClosePopupMenus();
	if(!GameClient()->m_GameConsole.IsActive())
		Input()->MouseModeRelative();
}

void CLazyClient::ConHudEditor(IConsole::IResult *pResult, void *pUserData)
{
	CLazyClient *pThis = static_cast<CLazyClient *>(pUserData);
	if(pResult->NumArguments() > 0 && pResult->GetInteger(0) == 0)
		pThis->CloseHudEditor(false);
	else if(pThis->m_HudEditorActive)
		pThis->CloseHudEditor(false);
	else
		pThis->OpenHudEditor();
}

void CLazyClient::ConHudReset(IConsole::IResult *pResult, void *pUserData)
{
	CLazyClient *pThis = static_cast<CLazyClient *>(pUserData);
	pThis->ResetHudLayout();
}

bool CLazyClient::HudModuleHidden(int ModuleId) const
{
	if(ModuleId < 0 || ModuleId >= HUD_MODULE_COUNT)
		return false;
	return m_aHudModules[ModuleId].m_Hidden;
}

bool CLazyClient::BeginHudModule(int ModuleId)
{
	if(ModuleId < 0 || ModuleId >= HUD_MODULE_COUNT)
		return false;
	if(!m_HudLayoutLoaded)
		LoadHudLayout();
	if(m_aHudModules[ModuleId].m_Hidden)
		return false;

	const CScreenRect Screen = Graphics()->GetScreen();
	if(m_HudScreenStackDepth < HUD_SCREEN_STACK_SIZE)
	{
		SHudSavedScreen &Saved = m_aHudScreenStack[m_HudScreenStackDepth++];
		Saved.m_TopLeft = Screen.m_TopLeft;
		Saved.m_BottomRight = Screen.m_BottomRight;
	}

	// Offsets are stored in standard 300-unit HUD space. Scale them to the
	// current mapping so scaled overlays move by the same visual distance.
	// Moving the screen rect in the opposite direction moves the rendered HUD
	// element with the mouse (positive editor offsets = right/down).
	const float Factor = Screen.Height() / 300.0f;
	const SHudModuleState &Module = m_aHudModules[ModuleId];
	Graphics()->MapScreen(Screen.Move(vec2(-Module.m_OffsetX * Factor, -Module.m_OffsetY * Factor)));
	return true;
}

void CLazyClient::EndHudModule()
{
	if(m_HudScreenStackDepth > 0)
	{
		const SHudSavedScreen &Saved = m_aHudScreenStack[--m_HudScreenStackDepth];
		Graphics()->MapScreen(CScreenRect(Saved.m_TopLeft, Saved.m_BottomRight));
	}
}

void CLazyClient::RenderHudEditor()
{
	if(!m_HudEditorActive || GameClient()->m_Scoreboard.IsActive())
		return;

	if(!m_HudLayoutLoaded)
		LoadHudLayout();

	const bool LiveHud = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;

	const CScreenRect OldScreen = Graphics()->GetScreen();
	Ui()->MapScreen();

	// The editor runs in absolute mouse mode so the OS cursor is free. Feed the
	// native cursor position into the UI and update it here because the menu is
	// inactive while the editor is open.
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const vec2 ScreenSize = Ui()->Screen()->Size();
	const vec2 CurrentWindow = Ui()->MousePos() * WindowSize / ScreenSize;
	const vec2 TargetWindow = Input()->NativeMousePos();
	Ui()->DisableMouseLock();
	Ui()->OnCursorMove(TargetWindow.x - CurrentWindow.x, TargetWindow.y - CurrentWindow.y);
	Ui()->StartCheck();
	Ui()->Update();

	const float UiWidth = Ui()->Screen()->w;
	const float UiHeight = Ui()->Screen()->h;
	const float HudWidth = 300.0f * Graphics()->ScreenAspect();
	const float HudHeight = 300.0f;
	const float Scale = UiHeight / HudHeight;
	const vec2 MouseHud = Ui()->MousePos() / Scale;

	auto ModuleRect = [&](int ModuleId) {
		const CUIRect &StoredRect = m_aHudModuleRects[ModuleId];
		const bool UseStored = LiveHud && StoredRect.w > 0.0f && StoredRect.h > 0.0f;
		const CUIRect HudRect = UseStored ? StoredRect :
			HudModuleRect(m_aHudModules[ModuleId], s_aHudModuleDefs[ModuleId], HudWidth, HudHeight);
		CUIRect UiRect = {HudRect.x * Scale, HudRect.y * Scale, HudRect.w * Scale, HudRect.h * Scale};
		return UiRect;
	};

	int Hovered = -1;
	if(m_HudEditorSettings < 0 && !m_HudEditorConfirm)
	{
		for(int i = HUD_MODULE_COUNT - 1; i >= 0; i--)
		{
			CUIRect Rect = ModuleRect(i);
			if(Ui()->MouseInside(&Rect))
			{
				Hovered = i;
				break;
			}
		}
	}

	if(m_HudEditorSettings < 0 && !m_HudEditorConfirm)
	{
		if(m_HudEditorDragging >= 0)
		{
			if(Ui()->MouseButton(0))
			{
				const vec2 Delta = MouseHud - m_HudEditorDragLast;
				m_aHudModules[m_HudEditorDragging].m_OffsetX = std::clamp(m_aHudModules[m_HudEditorDragging].m_OffsetX + round_to_int(Delta.x), -400, 400);
				m_aHudModules[m_HudEditorDragging].m_OffsetY = std::clamp(m_aHudModules[m_HudEditorDragging].m_OffsetY + round_to_int(Delta.y), -300, 300);
				m_HudEditorDragLast = MouseHud;
			}
			else
			{
				m_HudEditorDragging = -1;
			}
		}
		else if(Hovered >= 0 && Ui()->MouseButtonClicked(0))
		{
			if(Input()->ShiftIsPressed())
				m_aHudModules[Hovered].m_Hidden = !m_aHudModules[Hovered].m_Hidden;
			else
			{
				m_HudEditorDragging = Hovered;
				m_HudEditorDragLast = MouseHud;
			}
		}
		else if(Hovered >= 0 && Ui()->MouseButtonClicked(1))
		{
			m_HudEditorSettings = Hovered;
			m_HudEditorConfirm = false;
		}
		else if(Hovered < 0 && Ui()->MouseButtonClicked(1))
		{
			m_HudEditorConfirm = true;
			m_HudEditorSettings = -1;
		}
	}

	CUIRect Overlay = {0.0f, 0.0f, UiWidth, UiHeight};
	Overlay.Draw(ColorRGBA(0.02f, 0.03f, 0.05f, 0.22f), IGraphics::CORNER_NONE, 0.0f);

	for(int i = 0; i < HUD_MODULE_COUNT; i++)
	{
		CUIRect Rect = ModuleRect(i);
		const bool Hidden = m_aHudModules[i].m_Hidden;
		const bool Highlight = i == Hovered || i == m_HudEditorSettings;
		const ColorRGBA Fill = Hidden ?
			ColorRGBA(0.55f, 0.12f, 0.12f, Highlight ? 0.36f : 0.20f) :
			ColorRGBA(0.16f, 0.48f, 0.90f, Highlight ? 0.36f : 0.18f);
		const ColorRGBA Outline = Hidden ? ColorRGBA(1.0f, 0.38f, 0.38f, 0.95f) : ColorRGBA(0.48f, 0.80f, 1.0f, 0.95f);
		Rect.Draw(Fill, IGraphics::CORNER_ALL, 3.0f);
		Rect.DrawOutline(Outline);

		CUIRect Label = Rect;
		Label.h = std::min(Label.h, 26.0f);
		Label.Draw(ColorRGBA(0.02f, 0.03f, 0.05f, 0.78f), IGraphics::CORNER_T, 3.0f);
		char aLabel[128];
		if(Hidden)
			str_format(aLabel, sizeof(aLabel), "%s (hidden)", Localize(s_aHudModuleDefs[i].m_pName));
		else
			str_copy(aLabel, Localize(s_aHudModuleDefs[i].m_pName));
		Ui()->DoLabel(&Label, aLabel, 8.0f, TEXTALIGN_ML);
	}

	// Header/help bar.
	CUIRect HelpBar = {UiWidth / 2.0f - 360.0f, 8.0f, 720.0f, 38.0f};
	HelpBar.Draw(ColorRGBA(0.03f, 0.04f, 0.06f, 0.90f), IGraphics::CORNER_ALL, 6.0f);
	HelpBar.DrawOutline(ColorRGBA(0.45f, 0.70f, 1.0f, 0.75f));
	Ui()->DoLabel(&HelpBar, Localize("HUD Editor  —  drag to move  •  right-click a module for settings  •  Shift+left-click to hide/show  •  right-click empty space to save/discard"), 8.5f, TEXTALIGN_MC);

	if(!LiveHud)
	{
		CUIRect PreviewHint = {UiWidth / 2.0f - 360.0f, 48.0f, 720.0f, 20.0f};
		PreviewHint.Draw(ColorRGBA(0.03f, 0.04f, 0.06f, 0.80f), IGraphics::CORNER_ALL, 4.0f);
		Ui()->DoLabel(&PreviewHint, Localize("Preview mode  —  connect to a server to see the live HUD behind these outlines"), 8.0f, TEXTALIGN_MC);
	}

	static CButtonContainer s_SaveAndCloseButton;
	CUIRect CloseButton = {UiWidth - 190.0f, 10.0f, 180.0f, 24.0f};
	if(GameClient()->m_Menus.DoButton_Menu(&s_SaveAndCloseButton, Localize("Save and close"), 0, &CloseButton))
		CloseHudEditor(true);

	static CButtonContainer s_ResetLayoutButton;
	CUIRect ResetLayoutButton = {UiWidth - 190.0f, 40.0f, 180.0f, 22.0f};
	if(GameClient()->m_Menus.DoButton_Menu(&s_ResetLayoutButton, Localize("Reset layout"), 0, &ResetLayoutButton))
	{
		for(auto &Module : m_aHudModules)
			Module = SHudModuleState();
	}

	if(m_HudEditorConfirm)
	{
		CUIRect Panel = {UiWidth / 2.0f - 220.0f, UiHeight / 2.0f - 92.0f, 440.0f, 184.0f};
		Panel.Draw(ColorRGBA(0.04f, 0.05f, 0.08f, 0.97f), IGraphics::CORNER_ALL, 8.0f);
		Panel.DrawOutline(ColorRGBA(0.55f, 0.78f, 1.0f, 0.9f));
		CUIRect Inner = Panel;
		Inner.Margin(14.0f, &Inner);

		CUIRect Title;
		Inner.HSplitTop(28.0f, &Title, &Inner);
		Ui()->DoLabel(&Title, Localize("Save the HUD layout?"), 14.0f, TEXTALIGN_MC);
		Inner.HSplitTop(8.0f, nullptr, &Inner);
		CUIRect Hint;
		Inner.HSplitTop(34.0f, &Hint, &Inner);
		Ui()->DoLabel(&Hint, Localize("Save keeps your changes. Discard restores the layout from when you opened the editor."), 9.0f, TEXTALIGN_MC);

		CUIRect Buttons;
		Inner.HSplitBottom(30.0f, &Inner, &Buttons);
		Buttons.VSplitMid(&Buttons, nullptr, 8.0f);
		CUIRect SaveButton, DiscardButton;
		Buttons.VSplitMid(&SaveButton, &DiscardButton, 8.0f);

		static CButtonContainer s_SaveButton, s_DiscardButton, s_KeepEditingButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_SaveButton, Localize("Save"), 0, &SaveButton))
			CloseHudEditor(true);
		if(GameClient()->m_Menus.DoButton_Menu(&s_DiscardButton, Localize("Discard"), 0, &DiscardButton))
			CloseHudEditor(false);

		CUIRect Keep;
		Inner.HSplitBottom(30.0f, &Inner, &Keep);
		Keep = CUIRect{Panel.x + Panel.w / 2.0f - 90.0f, Panel.y + Panel.h - 72.0f, 180.0f, 24.0f};
		if(GameClient()->m_Menus.DoButton_Menu(&s_KeepEditingButton, Localize("Keep editing"), 0, &Keep))
			m_HudEditorConfirm = false;
	}

	if(m_HudEditorSettings >= 0 && m_HudEditorSettings < HUD_MODULE_COUNT)
	{
		const int ModuleId = m_HudEditorSettings;
		SHudModuleState &Module = m_aHudModules[ModuleId];

		CUIRect Panel = {UiWidth / 2.0f - 220.0f, UiHeight / 2.0f - 200.0f, 440.0f, 400.0f};
		Panel.Draw(ColorRGBA(0.04f, 0.05f, 0.08f, 0.97f), IGraphics::CORNER_ALL, 8.0f);
		Panel.DrawOutline(ColorRGBA(0.55f, 0.78f, 1.0f, 0.9f));
		CUIRect Inner = Panel;
		Inner.Margin(14.0f, &Inner);

		CUIRect Title, Row;
		Inner.HSplitTop(28.0f, &Title, &Inner);
		char aTitle[160];
		str_format(aTitle, sizeof(aTitle), "%s — %s", Localize("Module settings"), Localize(s_aHudModuleDefs[ModuleId].m_pName));
		Ui()->DoLabel(&Title, aTitle, 13.0f, TEXTALIGN_ML);
		Inner.HSplitTop(6.0f, nullptr, &Inner);

		Inner.HSplitTop(24.0f, &Row, &Inner);
		Ui()->DoScrollbarOption(&Module.m_OffsetX, &Module.m_OffsetX, &Row, Localize("Horizontal offset"), -400, 400, &CUi::ms_LinearScrollbarScale, 0, " px");
		Inner.HSplitTop(6.0f, nullptr, &Inner);
		Inner.HSplitTop(24.0f, &Row, &Inner);
		Ui()->DoScrollbarOption(&Module.m_OffsetY, &Module.m_OffsetY, &Row, Localize("Vertical offset"), -300, 300, &CUi::ms_LinearScrollbarScale, 0, " px");

		Inner.HSplitTop(8.0f, nullptr, &Inner);
		Inner.HSplitTop(24.0f, &Row, &Inner);
		Row.VSplitMid(&Row, nullptr, 8.0f);
		CUIRect ResetButton, HideButton;
		Row.VSplitMid(&ResetButton, &HideButton, 8.0f);
		static CButtonContainer s_ResetPositionButton;
		static CButtonContainer s_HideModuleButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_ResetPositionButton, Localize("Reset position"), 0, &ResetButton))
		{
			Module.m_OffsetX = 0;
			Module.m_OffsetY = 0;
		}
		if(GameClient()->m_Menus.DoButton_Menu(&s_HideModuleButton, Module.m_Hidden ? Localize("Show module") : Localize("Hide module"), 0, &HideButton))
			Module.m_Hidden = !Module.m_Hidden;

		unsigned int *pTextColor = nullptr;
		unsigned int *pBackgroundColor = nullptr;
		bool TextAlpha = false;
		bool BackgroundAlpha = false;
		ColorRGBA TextDefault(1.0f, 1.0f, 1.0f, 1.0f);
		ColorRGBA BackgroundDefault(0.0f, 0.0f, 0.0f, 1.0f);
		switch(ModuleId)
		{
		case HUD_MODULE_TELEMETRY:
			pTextColor = &g_Config.m_LcTelemetryTextColor;
			pBackgroundColor = &g_Config.m_LcTelemetryBgColor;
			TextDefault = color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::LcTelemetryTextColor, true));
			BackgroundDefault = color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::LcTelemetryBgColor, true));
			TextAlpha = true;
			BackgroundAlpha = true;
			break;
		case HUD_MODULE_ZERO_GRAV_WARNING:
			pTextColor = &g_Config.m_LcZeroGravTextColor;
			pBackgroundColor = &g_Config.m_LcZeroGravBgColor;
			TextDefault = color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::LcZeroGravTextColor, true));
			BackgroundDefault = color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::LcZeroGravBgColor, true));
			TextAlpha = true;
			BackgroundAlpha = true;
			break;
		case HUD_MODULE_STATUS_BAR:
			pTextColor = &g_Config.m_TcStatusBarTextColor;
			pBackgroundColor = &g_Config.m_TcStatusBarColor;
			// Matches the reset defaults used on the TClient status bar page.
			TextDefault = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
			BackgroundDefault = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
			break;
		default:
			break;
		}

		if(pTextColor != nullptr)
		{
			Inner.HSplitTop(12.0f, nullptr, &Inner);
			CUIRect ColorsTitle;
			Inner.HSplitTop(20.0f, &ColorsTitle, &Inner);
			Ui()->DoLabel(&ColorsTitle, Localize("Colors"), 10.0f, TEXTALIGN_ML);

			static CButtonContainer s_TextColorResetButton;
			static CButtonContainer s_BackgroundColorResetButton;
			Inner.HSplitTop(2.0f, nullptr, &Inner);
			GameClient()->m_Menus.DoLine_ColorPicker(&s_TextColorResetButton, 22.0f, 9.0f, 6.0f, &Inner, Localize("Text color"), pTextColor, TextDefault, false, nullptr, TextAlpha);
			if(pBackgroundColor != nullptr)
				GameClient()->m_Menus.DoLine_ColorPicker(&s_BackgroundColorResetButton, 22.0f, 9.0f, 6.0f, &Inner, Localize("Background color"), pBackgroundColor, BackgroundDefault, false, nullptr, BackgroundAlpha);
		}
		else
		{
			Inner.HSplitTop(12.0f, nullptr, &Inner);
			CUIRect Hint;
			Inner.HSplitTop(30.0f, &Hint, &Inner);
			Ui()->DoLabel(&Hint, Localize("This HUD group uses DDNet's fixed colors. Move or hide it here."), 9.0f, TEXTALIGN_ML);
		}

		static CButtonContainer s_ResetModuleButton, s_SettingsCloseButton;
		CUIRect Bottom;
		Inner.HSplitBottom(28.0f, &Inner, &Bottom);
		Bottom.VSplitMid(&Bottom, nullptr, 8.0f);
		CUIRect ResetModule, CloseSettings;
		Bottom.VSplitMid(&ResetModule, &CloseSettings, 8.0f);
		if(GameClient()->m_Menus.DoButton_Menu(&s_ResetModuleButton, Localize("Reset module"), 0, &ResetModule))
		{
			Module = SHudModuleState();
		}
		if(GameClient()->m_Menus.DoButton_Menu(&s_SettingsCloseButton, Localize("Close"), 0, &CloseSettings))
			m_HudEditorSettings = -1;
	}

	Ui()->RenderPopupMenus();
	Ui()->FinishCheck();

	Graphics()->MapScreen(OldScreen);
}

void CLazyClient::OnRender()
{
	if(!m_HudLayoutLoaded)
		LoadHudLayout();
	RenderZeroGravityForcedStatus();
	RenderZeroGravityWarning();
	RenderTelemetry();
	RenderSwapTimers();
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
	if(UseDevIcon())
		DevIconTexture(); // Load before the first nameplate/scoreboard render.
	UpdateBestClientList();
	UpdateAutoMargin();
	UpdateAntiping();
	UpdateAspectConfirm();
	UpdateZoomKeyBinds();
	UpdateZeroGravityPredictor();
	UpdateAutoTeamLock();
	UpdateAutoKill();
	UpdateTelemetry();
	UpdateTeamLoadQueue();
}
