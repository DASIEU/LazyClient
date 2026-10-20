#include "lazy.h"

#include <base/math.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/components/binds.h>
#include <game/client/components/controls.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

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
	m_GoresAttackEnd = 0;
	m_GunKeyHeld = false;
	ResetAutoMarginState();
}

void CLazyClient::OnStateChange(int NewState, int OldState)
{
	if(NewState == OldState)
		return;
	ResetAutoMarginState();
	m_GoresHeavyWeapon = false;
	m_GoresAttacking = false;
	m_GunKeyHeld = false;
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
	m_aSnapPrevLeft[Dummy] = false;
	m_aSnapPrevRight[Dummy] = false;
}

int CLazyClient::ResolveMovementDirection(int Dummy, bool LeftPressed, bool RightPressed, bool UpdateState)
{
	if(UpdateState)
	{
		const int64_t Now = time_get();
		const bool AnyPressedBefore = m_aSnapPrevLeft[Dummy] || m_aSnapPrevRight[Dummy];
		if(LeftPressed && !m_aSnapPrevLeft[Dummy])
		{
			m_aSnapLastPressed[Dummy] = -1;
			m_aSnapLastPressedTime[Dummy] = Now;
		}
		if(RightPressed && !m_aSnapPrevRight[Dummy])
		{
			m_aSnapLastPressed[Dummy] = 1;
			m_aSnapLastPressedTime[Dummy] = Now;
		}
		m_aSnapPrevLeft[Dummy] = LeftPressed;
		m_aSnapPrevRight[Dummy] = RightPressed;
		if(!LeftPressed && !RightPressed && AnyPressedBefore)
			m_aSnapLastPressedTime[Dummy] = Now;
	}

	if(LeftPressed == RightPressed)
	{
		if(!LeftPressed)
		{
			if(g_Config.m_LcSnapTap && m_aSnapApplied[Dummy] != 0 && g_Config.m_LcSnapTapSmoothness > 0)
			{
				const int64_t Grace = (time_freq() * (int64_t)g_Config.m_LcSnapTapSmoothness) / 1000;
				if(time_get() - m_aSnapLastPressedTime[Dummy] < Grace)
					return m_aSnapApplied[Dummy];
			}
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
			if(time_get() - m_aSnapLastPressedTime[Dummy] >= Delay)
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
		pThis->m_GoresAttacking = false;

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

	if(!g_Config.m_LcGoresMode || !GameClient()->m_Snap.m_pLocalCharacter)
	{
		m_GoresHeavyWeapon = false;
		m_GoresAttacking = false;
		return;
	}

	const int CurWeapon = GameClient()->m_Snap.m_pLocalCharacter->m_Weapon;
	m_GoresHeavyWeapon = CurWeapon == WEAPON_SHOTGUN || CurWeapon == WEAPON_GRENADE ||
			     CurWeapon == WEAPON_LASER || CurWeapon == WEAPON_NINJA;
	if(g_Config.m_LcGoresDisableIfWeapons && m_GoresHeavyWeapon)
	{
		m_GoresAttacking = false;
		return;
	}

	if(m_GoresAttacking && time_get() >= m_GoresAttackEnd)
		m_GoresAttacking = false;

	if(m_GunKeyHeld)
	{
		Controls.m_aInputData[Dummy].m_WantedWeapon = WEAPON_GUN + 1;
		return;
	}

	Controls.m_aInputData[Dummy].m_WantedWeapon = m_GoresAttacking ? WEAPON_HAMMER + 1 : WEAPON_GUN + 1;
}

void CLazyClient::OnFireInput(int Pressed)
{
	if(!g_Config.m_LcGoresMode)
		return;
	if(g_Config.m_LcGoresDisableIfWeapons && m_GoresHeavyWeapon)
		return;

	if(Pressed)
	{
		m_GoresAttacking = true;
		m_GoresAttackEnd = time_get() + (time_freq() * g_Config.m_LcGoresSwitchDelay) / 1000;
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

void CLazyClient::ApplyAspect(int Value)
{
	m_AspectApplied = Value;
	Graphics()->SetCustomAspect(Value > 0 ? Value / 100.0f : 0.0f);
}

void CLazyClient::UpdateAspectConfirm()
{
	if(!m_AspectSeeded)
	{
		m_AspectSeeded = true;
		m_AspectLast = g_Config.m_LcCustomAspect;
		m_AspectPrev = m_AspectLast;
		ApplyAspect(m_AspectLast);
		return;
	}

	const int Current = g_Config.m_LcCustomAspect;
	if(Current != m_AspectLast)
	{
		if(Current > 0)
		{
			if(!m_AspectConfirm)
				m_AspectPrev = m_AspectLast;
			m_AspectConfirm = true;
			m_AspectDeadline = time_get() + time_freq() * ASPECT_CONFIRM_SECONDS;
		}
		else
		{
			m_AspectConfirm = false;
			m_AspectPrev = 0;
		}
		m_AspectLast = Current;
		ApplyAspect(Current);
	}

	if(m_AspectConfirm && time_get() >= m_AspectDeadline)
		RevertAspect();
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
	m_AspectPrev = g_Config.m_LcCustomAspect;
	m_AspectLast = m_AspectPrev;
}

void CLazyClient::RevertAspect()
{
	g_Config.m_LcCustomAspect = m_AspectPrev;
	m_AspectLast = m_AspectPrev;
	m_AspectConfirm = false;
	ApplyAspect(m_AspectPrev);
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

	UpdateAutoMargin();
	UpdateAspectConfirm();
}
