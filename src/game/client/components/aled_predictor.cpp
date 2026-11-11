#include "aled_predictor.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

#include <algorithm>

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
	if(Target.m_FreezeEnd <= 0 || length(Target.m_Vel) <= 2.0f)
		return false;
	if(FreezeTileAt(Collision(), Target.m_Pos) != EFreezeTile::NORMAL)
		return false;
	if(TouchesDeath(Collision(), Target.m_Pos))
		return false;

	// The hitter must be able to use the hammer right now.
	if(Hitter.m_DeepFrozen || Hitter.m_LiveFrozen || Hitter.m_FreezeEnd > 0)
		return false;
	if(Hitter.m_HammerHitDisabled)
		return false;

	const int NowTick = Client()->GameTick(g_Config.m_ClDummy);
	const int TicksUntilUnfreeze = Target.m_FreezeEnd - NowTick;
	if(TicksUntilUnfreeze <= 1)
		return false;

	CCharacterCore Sim = Target;
	Sim.Init(nullptr, Collision());
	Sim.m_Input.m_Direction = 0;
	Sim.m_Input.m_Jump = 0;
	Sim.m_Input.m_Hook = 0;
	Sim.m_Direction = 0;
	Sim.m_HookState = HOOK_IDLE;
	Sim.m_HookPos = Sim.m_Pos;
	Sim.SetHookedPlayer(-1);

	const int MaxTicks = std::min(TicksUntilUnfreeze - 1, 3 * SERVER_TICK_SPEED);
	bool Exited = false;
	for(int Tick = 1; Tick <= MaxTicks; ++Tick)
	{
		Sim.Tick(true, false);
		Sim.Move();

		if(Tick >= TicksUntilUnfreeze)
			break;

		const EFreezeTile Tile = FreezeTileAt(Collision(), Sim.m_Pos);
		if(Tile == EFreezeTile::DEATH || Tile == EFreezeTile::DEEP || Tile == EFreezeTile::LIVE)
			break;
		if(TouchesDeath(Collision(), Sim.m_Pos))
			break;

		if(Tile == EFreezeTile::NORMAL)
		{
			// Re-entering freeze ends the usable window.
			if(Exited)
				break;
			continue;
		}

		if(!InHammerRange(Hitter.m_Pos, Sim.m_Pos))
		{
			if(Exited)
				break;
			// The exit itself must already be hammerable, otherwise this is not a usable ALED.
			return false;
		}

		if(!Exited)
		{
			Exited = true;
			pOut->m_ExitPos = Sim.m_Pos;
			pOut->m_ExitTick = Tick;
		}
		pOut->m_LastHitPos = Sim.m_Pos;
		pOut->m_LastHitTick = Tick;
	}

	if(!Exited)
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

void CAledPredictor::RenderPrediction(const SPrediction &Prediction) const
{
	const ColorRGBA ExitColor(0.20f, 1.00f, 0.75f, 0.65f);
	const ColorRGBA LastColor(1.00f, 0.60f, 0.15f, 0.55f);

	Graphics()->TextureClear();
	Graphics()->LinesBegin();
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

void CAledPredictor::OnRender()
{
	if(!g_Config.m_LcAledPredictor)
		return;
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	SPrediction Prediction;
	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(LocalId < 0)
			continue;
		const int HitterId = FindHitter(LocalId);
		if(HitterId < 0)
			continue;
		if(PredictForTarget(LocalId, HitterId, &Prediction))
			break;
	}

	if(Prediction.m_Valid)
		RenderPrediction(Prediction);
}
