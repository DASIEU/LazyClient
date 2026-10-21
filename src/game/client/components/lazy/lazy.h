#ifndef GAME_CLIENT_COMPONENTS_LAZY_LAZY_H
#define GAME_CLIENT_COMPONENTS_LAZY_LAZY_H

#include <base/types.h>

#include <engine/client/enums.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

class CCollision;
class IHttpRequest;

class CLazyClient : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnNewSnapshot() override;
	void OnUpdate() override;
	void OnRender() override;

	// Client identification
	void ApplyClientTypeToColor(int &ColorBody, int &ColorFeet) const;
	int ClientType(int ClientId) const;
	IGraphics::CTextureHandle ClientIcon(int Type) const;
	void RenderClientIcon(int Type, vec2 Pos, float Size, float Alpha) const;

	// Input modes
	float InputOffsetTicks() const;
	bool ApplyInputToOthers() const;
	bool InputModeActive() const;

	// Snap tap
	void ResetSnapTap(int Dummy);
	void OnMovementKey(bool Left, bool Pressed);
	int ResolveMovementDirection(int Dummy, bool LeftPressed, bool RightPressed);

	// Gores mode
	void UpdateGoresMode();
	void OnFireInput(int Pressed, bool Changed);
	void OnWeaponSelectInput();
	void BindGunKey();

	// Antiping
	void UpdateAntiping();

	// Aspect ratio
	bool AspectConfirmActive() const { return m_AspectConfirm; }
	int AspectSecondsLeft() const;
	void KeepAspect();
	void RevertAspect();

private:
	static void ConGunFire(IConsole::IResult *pResult, void *pUserData);
	void BindGunKeyForced();
	void RestoreGunKey();
	void RestoreStaleGunKeyBind();

	// Gores mode
	bool m_GoresHeavyWeapon = false;
	bool m_GoresAttacking = false;
	bool m_GoresFireHeld = false;
	bool m_GoresInjectedFire = false;
	bool m_GoresInitialized = false;
	int m_GoresAttackStage = 0;
	int64_t m_GoresAttackEnd = 0; // hammer-select deadline, then switch-back deadline
	int64_t m_GoresWeaponSelectUntil = 0;
	int m_GoresForcedWeapon = 0;
	bool m_GunKeyHeld = false;
	bool m_GoresKeyBound = false;
	bool m_GoresWasEnabled = false;
	int m_GoresBoundKey = 0;
	char m_aGoresPrevBind[256] = {};

	// Snap tap
	int m_aSnapApplied[NUM_DUMMIES] = {};
	int m_aSnapLastPressed[NUM_DUMMIES] = {};
	int64_t m_aSnapLastPressedTime[NUM_DUMMIES] = {};

	// Auto margin
	float m_CheckTimer = 0.0f;
	float m_LatestPing = -1.0f;
	float m_IntervalPingSum = 0.0f;
	int m_IntervalPingSamples = 0;
	float m_IntervalMinPing = -1.0f;
	float m_IntervalPeakPing = -1.0f;
	float m_SmoothedPing = -1.0f;
	float m_SmoothedJitter = 0.0f;
	bool m_HighPing = false;
	bool m_WasAutoMarginEnabled = false;
	int m_SavedMargin = -1;

	// Antipiping
	float m_AntipingSmoothedPing = -1.0f;

	// Aspect ratio
	bool m_AspectSeeded = false;
	bool m_AspectConfirm = false;
	int64_t m_AspectDeadline = 0;
	int m_AspectLastWidth = 0;
	int m_AspectLastHeight = 0;
	int m_AspectPrevWidth = 0;
	int m_AspectPrevHeight = 0;

	// Client identification
	int m_aClientTypes[MAX_CLIENTS] = {};
	bool m_ClientLogosInitialized = false;
	bool m_WasClientLogosEnabled = false;
	std::shared_ptr<IHttpRequest> m_pBestClientListTask;
	float m_BestClientListRefreshTimer = 0.0f;
	bool m_BestClientListLoaded = false;
	std::unordered_map<std::string, std::unordered_set<std::string>> m_BestClientPlayers;

	// Zero gravity freeze predictor
	float m_ZeroGravCheckTimer = 0.0f;
	bool m_ZeroGravWarning = false;
	float m_ZeroGravSeconds = 0.0f;

	void ResetAutoMarginState();
	void UpdateAutoMargin();
	void ApplyAspect(int Width, int Height);
	void UpdateAspectConfirm();
	void UpdateClientTypes();
	void UpdateClientLogoState();
	void UpdateBestClientList();
	void FinishBestClientList();
	void ResetBestClientList();
	void UpdateZeroGravityPredictor();
	bool PathHitsFreeze(CCollision *pCollision, vec2 PrevPos, vec2 Pos) const;
	void RenderZeroGravityWarning();
};

#endif
