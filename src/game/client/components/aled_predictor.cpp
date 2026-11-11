#include "aled_predictor.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>

namespace
{
	enum class EFreezeTile
	{
		NONE,
		NORMAL,
		DEEP,
		LIVE,
		DEATH,
	};

	EFreezeTile FreezeTileAt(const CCollision *pCollision, vec2 Pos)
	{
		const int Index = pCollision->GetPureMapIndex(Pos);
		const int aTiles[] = {
			pCollision->GetTileIndex(Index),
			pCollision->GetFrontTileIndex(Index),
			pCollision->GetSwitchType(Index)};

		EFreezeTile Result = EFreezeTile::NONE;
		for(const int Tile : aTiles)
		{
			if(Tile == TILE_DEATH)
				return EFreezeTile::DEATH;
			if(Tile == TILE_DFREEZE)
				return EFreezeTile::DEEP;
			if(Tile == TILE_LFREEZE)
				return EFreezeTile::LIVE;
			if(Tile == TILE_FREEZE)
				Result = EFreezeTile::NORMAL;
		}
		return Result;
	}

	bool TouchesDeath(const CCollision *pCollision, vec2 Pos)
	{
		const float Offset = CCharacterCore::PhysicalSize() / 3.0f;
		for(const float dx : {-Offset, Offset})
		{
			for(const float dy : {-Offset, Offset})
			{
				const vec2 Corner = Pos + vec2(dx, dy);
				if(pCollision->GetCollisionAt(Corner.x, Corner.y) == TILE_DEATH || pCollision->GetFrontCollisionAt(Corner.x, Corner.y) == TILE_DEATH)
					return true;
			}
		}
		return false;
	}

	bool InHammerRange(vec2 HitterPos, vec2 TargetPos)
	{
		const vec2 Diff = TargetPos - HitterPos;
		const float Distance = length(Diff);
		if(Distance < 0.001f)
			return true;
		const vec2 ProjStartPos = HitterPos + Diff / Distance * (CCharacterCore::PhysicalSize() * 0.75f);
		return distance(TargetPos, ProjStartPos) < CCharacterCore::PhysicalSize() * 1.5f;
	}

	bool HasNormalFreezeNearby(const CCollision *pCollision, vec2 Pos, vec2 Vel)
	{
		// Cheap early-out before running the physics simulation. ALED only
		// matters when there is a normal freeze tile close enough to reach
		// soon, so avoid simulating tees that are far away from any freeze.
		const float ReachPerSecond = length(Vel);
		const int RadiusTiles = std::clamp(3 + (int)std::ceil(ReachPerSecond * 1.5f / 32.0f), 3, 16);
		const int CenterX = round_to_int(Pos.x) / 32;
		const int CenterY = round_to_int(Pos.y) / 32;
		for(int y = CenterY - RadiusTiles; y <= CenterY + RadiusTiles; ++y)
		{
			for(int x = CenterX - RadiusTiles; x <= CenterX + RadiusTiles; ++x)
			{
				if(FreezeTileAt(pCollision, vec2(x * 32 + 16.0f, y * 32 + 16.0f)) == EFreezeTile::NORMAL)
					return true;
			}
		}
		return false;
	}
} // namespace

int CAledPredictor::FindHitter(int TargetId) const
{
	const auto IsUsableHitter = [&](int ClientId) {
		if(ClientId < 0 || ClientId == TargetId)
			return false;
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			return false;
		if(!GameClient()->m_Teams.CanCollide(TargetId, ClientId))
			return false;
		const CCharacterCore &Hitter = GameClient()->m_aClients[ClientId].m_Predicted;
		return !Hitter.m_DeepFrozen && !Hitter.m_LiveFrozen && Hitter.m_FreezeEnd <= 0 && !Hitter.m_HammerHitDisabled;
	};

	// Prefer the other local tee because its prediction is the most reliable.
	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(IsUsableHitter(LocalId))
			return LocalId;
	}

	const CCharacterCore &Target = GameClient()->m_aClients[TargetId].m_Predicted;
	int BestId = -1;
	float BestDistance = 300.0f;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!IsUsableHitter(ClientId))
			continue;
		const float Distance = distance(Target.m_Pos, GameClient()->m_aClients[ClientId].m_Predicted.m_Pos);
		if(Distance < BestDistance)
		{
			BestDistance = Distance;
			BestId = ClientId;
		}
	}
	return BestId;
}

bool CAledPredictor::PredictForTarget(int TargetId, int HitterId, SPrediction *pOut) const
{
	if(TargetId < 0 || HitterId < 0 || TargetId == HitterId)
		return false;
	if(!GameClient()->m_Snap.m_aCharacters[TargetId].m_Active || !GameClient()->m_Snap.m_aCharacters[HitterId].m_Active)
		return false;
	if(!GameClient()->m_Teams.CanCollide(TargetId, HitterId))
		return false;

	const CCharacterCore &Target = GameClient()->m_aClients[TargetId].m_Predicted;
	const CCharacterCore &Hitter = GameClient()->m_aClients[HitterId].m_Predicted;

	// Only normal freeze can be ALED. Deep freeze, live freeze and death are fake scenarios.
	if(Target.m_DeepFrozen || Target.m_LiveFrozen || Target.m_Super || Target.m_Invincible)
		return false;
	if(length(Target.m_Vel) <= 2.0f)
		return false;

	const int NowTick = Client()->GameTick(g_Config.m_ClDummy);
	const EFreezeTile StartTile = FreezeTileAt(Collision(), Target.m_Pos);
	if(StartTile == EFreezeTile::DEATH || StartTile == EFreezeTile::DEEP || StartTile == EFreezeTile::LIVE)
		return false;
	if(TouchesDeath(Collision(), Target.m_Pos))
		return false;

	const bool StartedFrozen = Target.m_FreezeEnd > NowTick;
	const bool TreatAsFrozen = StartedFrozen || StartTile == EFreezeTile::NORMAL;

	if(!TreatAsFrozen && !HasNormalFreezeNearby(Collision(), Target.m_Pos, Target.m_Vel))
		return false;

	// The hitter must be able to use the hammer right now.
	if(Hitter.m_DeepFrozen || Hitter.m_LiveFrozen || Hitter.m_FreezeEnd > 0)
		return false;
	if(Hitter.m_HammerHitDisabled)
		return false;

	CCharacterCore Sim = Target;
	Sim.Init(nullptr, Collision());
	if(TreatAsFrozen)
	{
		// Frozen tees keep their velocity, but movement/jump/hook input is cleared.
		Sim.m_Input.m_Direction = 0;
		Sim.m_Input.m_Jump = 0;
		Sim.m_Input.m_Hook = 0;
		Sim.m_Direction = 0;
	}
	Sim.m_HookState = HOOK_IDLE;
	Sim.m_HookPos = Sim.m_Pos;
	Sim.SetHookedPlayer(-1);

	const int FreezeTicks = std::max(1, g_Config.m_SvFreezeDelay) * SERVER_TICK_SPEED;
	int SimFreezeEnd = StartedFrozen ? Target.m_FreezeEnd : (StartTile == EFreezeTile::NORMAL ? NowTick + FreezeTicks : NowTick);
	bool InFreeze = StartTile == EFreezeTile::NORMAL;
	bool LeftFreeze = false;
	bool Started = false;

	const int MaxTicks = 3 * SERVER_TICK_SPEED;
	for(int Tick = 1; Tick <= MaxTicks; ++Tick)
	{
		Sim.Tick(true, false);
		Sim.Move();

		const int AbsTick = NowTick + Tick;
		const EFreezeTile Tile = FreezeTileAt(Collision(), Sim.m_Pos);
		if(Tile == EFreezeTile::DEATH || Tile == EFreezeTile::DEEP || Tile == EFreezeTile::LIVE)
			break;
		if(TouchesDeath(Collision(), Sim.m_Pos))
			break;

		if(Tile == EFreezeTile::NORMAL)
		{
			if(!InFreeze)
			{
				InFreeze = true;
				// Entering normal freeze starts (or refreshes) the freeze timer.
				SimFreezeEnd = std::max(SimFreezeEnd, AbsTick + FreezeTicks);
			}
			if(LeftFreeze)
				break; // re-entering freeze ends the usable window
			continue;
		}

		if(InFreeze)
		{
			InFreeze = false;
			LeftFreeze = true;
		}

		if(!LeftFreeze)
			continue; // still approaching the freeze wall

		if(AbsTick >= SimFreezeEnd)
			break; // natural unfreeze before a usable hit

		if(InHammerRange(Hitter.m_Pos, Sim.m_Pos))
		{
			if(!Started)
			{
				Started = true;
				pOut->m_ExitPos = Sim.m_Pos;
				pOut->m_ExitTick = AbsTick;
			}
			pOut->m_LastHitPos = Sim.m_Pos;
			pOut->m_LastHitTick = AbsTick;
		}
		else if(Started)
		{
			break; // left the hammer's reach
		}
	}

	if(!Started)
		return false;

	pOut->m_Valid = true;
	pOut->m_TargetId = TargetId;
	pOut->m_HitterId = HitterId;
	return true;
}

void CAledPredictor::RenderGhost(int ClientId, vec2 Pos, float Alpha) const
{
	CTeeRenderInfo Info = GameClient()->m_aClients[ClientId].m_RenderInfo;
	Info.m_TeeRenderFlags = TEE_EFFECT_FROZEN | TEE_NO_WEAPON;
	Info.m_Size = 1.0f;
	RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, vec2(1.0f, 0.0f), Pos, Alpha);
}

void CAledPredictor::RenderPrediction(const SPrediction &Prediction, int NowTick) const
{
	const ColorRGBA ExitColor(0.20f, 1.00f, 0.75f, 0.65f);
	const ColorRGBA LastColor(1.00f, 0.60f, 0.15f, 0.55f);

	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	if(NowTick < Prediction.m_ExitTick)
	{
		// Show the remaining throw path so the future hit point is visible early.
		const vec2 TargetPos = GameClient()->m_aClients[Prediction.m_TargetId].m_RenderPos;
		Graphics()->SetColor(ExitColor.WithAlpha(0.25f));
		const IGraphics::CLineItem PathLine(
			TargetPos.x, TargetPos.y,
			Prediction.m_ExitPos.x, Prediction.m_ExitPos.y);
		Graphics()->LinesDraw(&PathLine, 1);
	}
	if(distance(Prediction.m_ExitPos, Prediction.m_LastHitPos) > 0.5f)
	{
		Graphics()->SetColor(ExitColor);
		const IGraphics::CLineItem Line(
			Prediction.m_ExitPos.x, Prediction.m_ExitPos.y,
			Prediction.m_LastHitPos.x, Prediction.m_LastHitPos.y);
		Graphics()->LinesDraw(&Line, 1);
	}
	Graphics()->LinesEnd();

	Graphics()->QuadsBegin();
	Graphics()->SetColor(ExitColor);
	Graphics()->DrawCircle(Prediction.m_ExitPos.x, Prediction.m_ExitPos.y, 18.0f, 20);
	Graphics()->SetColor(LastColor);
	Graphics()->DrawCircle(Prediction.m_LastHitPos.x, Prediction.m_LastHitPos.y, 18.0f, 20);
	Graphics()->QuadsEnd();

	RenderGhost(Prediction.m_TargetId, Prediction.m_ExitPos, 0.65f);
	if(distance(Prediction.m_ExitPos, Prediction.m_LastHitPos) > 0.5f)
		RenderGhost(Prediction.m_TargetId, Prediction.m_LastHitPos, 0.35f);
}

void CAledPredictor::ValidatePrediction(int NowTick)
{
	if(!m_CachedPrediction.m_Valid)
		return;

	const int TargetId = m_CachedPrediction.m_TargetId;
	const int HitterId = m_CachedPrediction.m_HitterId;
	const CCharacterCore &Target = GameClient()->m_aClients[TargetId].m_Predicted;
	const CCharacterCore &Hitter = GameClient()->m_aClients[HitterId].m_Predicted;
	const bool BeforeExit = NowTick < m_CachedPrediction.m_ExitTick;
	if(NowTick > m_CachedPrediction.m_LastHitTick ||
		!GameClient()->m_Snap.m_aCharacters[TargetId].m_Active ||
		!GameClient()->m_Snap.m_aCharacters[HitterId].m_Active ||
		Target.m_DeepFrozen || Target.m_LiveFrozen ||
		(!BeforeExit && Target.m_FreezeEnd <= NowTick) ||
		Hitter.m_DeepFrozen || Hitter.m_LiveFrozen || Hitter.m_FreezeEnd > 0 ||
		Hitter.m_HammerHitDisabled)
	{
		m_CachedPrediction.m_Valid = false;
	}
}

void CAledPredictor::OnRender()
{
	if(!g_Config.m_LcAledPredictor)
	{
		m_CachedPrediction.m_Valid = false;
		m_LastComputedTick = -1;
		return;
	}
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		m_CachedPrediction.m_Valid = false;
		m_LastComputedTick = -1;
		return;
	}

	const int NowTick = Client()->GameTick(g_Config.m_ClDummy);

	// The cached window can be validated cheaply every frame, but the expensive
	// simulation only needs to run once per game tick and only when no usable
	// window is cached yet. This keeps ALED rendering independent of the frame
	// rate instead of simulating up to 3 seconds of physics on every frame.
	ValidatePrediction(NowTick);
	if(!m_CachedPrediction.m_Valid && NowTick != m_LastComputedTick)
	{
		m_LastComputedTick = NowTick;

		SPrediction Prediction;
		bool Found = false;
		for(const int LocalId : GameClient()->m_aLocalIds)
		{
			if(LocalId < 0)
				continue;
			const int HitterId = FindHitter(LocalId);
			if(HitterId < 0)
				continue;
			if(PredictForTarget(LocalId, HitterId, &Prediction))
			{
				m_CachedPrediction = Prediction;
				Found = true;
				break;
			}
		}
		if(!Found)
			m_CachedPrediction.m_Valid = false;
	}

	if(m_CachedPrediction.m_Valid)
		RenderPrediction(m_CachedPrediction, NowTick);
}
