#ifndef GAME_CLIENT_COMPONENTS_ALED_PREDICTOR_H
#define GAME_CLIENT_COMPONENTS_ALED_PREDICTOR_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

class CAledPredictor : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;

private:
	struct SPrediction
	{
		bool m_Valid = false;
		int m_TargetId = -1;
		int m_HitterId = -1;
		vec2 m_ExitPos = vec2(0.0f, 0.0f);
		vec2 m_LastHitPos = vec2(0.0f, 0.0f);
		int m_ExitTick = 0;
		int m_LastHitTick = 0;
	};

	int FindHitter(int TargetId) const;
	bool PredictForTarget(int TargetId, int HitterId, SPrediction *pOut) const;
	void RenderPrediction(const SPrediction &Prediction) const;
	void RenderGhost(int ClientId, vec2 Pos, float Alpha) const;
};

#endif
