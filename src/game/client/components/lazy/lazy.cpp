#include "lazy.h"

#include "lazy_ccid.h"

#include <base/math.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
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

bool IsHeavyWeapon(int Weapon)
{
	return Weapon == WEAPON_SHOTGUN || Weapon == WEAPON_GRENADE || Weapon == WEAPON_LASER || Weapon == WEAPON_NINJA;
}

bool IsFreezeTile(int Tile)
{
	return Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE;
}

float MarginFromPing(float Ping)
{
	return std::max((float)AUTO_MARGIN_BASE, Ping * 0.25f);
}
} // namespace

void CLazyClient::OnConsoleInit()
{
	Console()->Register("+lc_gun", "", CFGFLAG_CLIENT, ConGunFire, this, "Fire the gun in gores mode without using the hammer");
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
	m_ZeroGravWarning = false;
	m_ZeroGravCheckTimer = 0.0f;
	ResetAutoMarginState();
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
	m_ZeroGravWarning = false;
	m_ZeroGravCheckTimer = 0.0f;
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

void CLazyClient::UpdateZeroGravityPredictor()
{
	if(!g_Config.m_LcZeroGravPredict || Client()->State() != IClient::STATE_ONLINE ||
		!GameClient()->m_Snap.m_pLocalCharacter || GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		m_ZeroGravWarning = false;
		m_ZeroGravCheckTimer = 0.0f;
		return;
	}

	const CCharacterCore &Core = GameClient()->m_PredictedChar;
	if(Core.m_HookState != HOOK_GRABBED || Core.HookedPlayer() != -1 ||
		std::abs(Core.m_Tuning.m_Gravity) > 0.001f ||
		Core.m_FreezeEnd > 0 || Core.m_DeepFrozen || Core.m_LiveFrozen || Core.m_IsInFreeze ||
		Core.m_Super || Core.m_Invincible)
	{
		m_ZeroGravWarning = false;
		m_ZeroGravCheckTimer = 0.0f;
		return;
	}

	m_ZeroGravCheckTimer -= Client()->RenderFrameTime();
	if(m_ZeroGravCheckTimer > 0.0f)
		return;
	m_ZeroGravCheckTimer = 0.05f;

	const int TickSpeed = std::max(1, Client()->GameTickSpeed());
	const int MaxTicks = std::max(1, g_Config.m_LcZeroGravPredictTime) * TickSpeed;

	CCharacterCore Sim = Core;
	Sim.SetCoreWorld(nullptr, Collision(), nullptr);
	Sim.m_Input.m_Hook = 1;

	vec2 PrevPos = Sim.m_Pos;
	for(int Tick = 1; Tick <= MaxTicks; Tick++)
	{
		Sim.Tick(true, false);
		Sim.Move();
		Sim.Quantize();

		if(PathHitsFreeze(Collision(), PrevPos, Sim.m_Pos))
		{
			m_ZeroGravWarning = true;
			m_ZeroGravSeconds = Tick / (float)TickSpeed;
			return;
		}
		PrevPos = Sim.m_Pos;
	}

	m_ZeroGravWarning = false;
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

	char aBuf[64];
	if(m_ZeroGravSeconds < 0.2f)
		str_copy(aBuf, Localize("Freeze now"));
	else
		str_format(aBuf, sizeof(aBuf), Localize("Freeze in %.1fs"), m_ZeroGravSeconds);

	const float FontSize = 12.0f;
	const float TextWidth = TextRender()->TextWidth(FontSize, aBuf);
	CUIRect Box = {Width / 2.0f - TextWidth / 2.0f - 8.0f, 38.0f, TextWidth + 16.0f, FontSize + 8.0f};
	Box.Draw(ColorRGBA(0.05f, 0.05f, 0.05f, 0.7f), IGraphics::CORNER_ALL, 3.0f);

	TextRender()->TextColor(ColorRGBA(1.0f, 0.25f, 0.2f, 1.0f));
	Ui()->DoLabel(&Box, aBuf, FontSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	Graphics()->MapScreen(OldScreen);
}

void CLazyClient::OnRender()
{
	RenderZeroGravityWarning();
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

	UpdateClientLogoState();
	UpdateAutoMargin();
	UpdateAntiping();
	UpdateAspectConfirm();
	UpdateZeroGravityPredictor();
}
