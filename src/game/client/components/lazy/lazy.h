#ifndef GAME_CLIENT_COMPONENTS_LAZY_LAZY_H
#define GAME_CLIENT_COMPONENTS_LAZY_LAZY_H

#include <base/types.h>

#include <engine/client/enums.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/gamecore.h>

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
	void OnMessage(int MsgType, void *pRawMsg) override;
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
	bool HasInputLead() const;

	// Snap tap
	void ResetSnapTap(int Dummy);
	void OnMovementKey(bool Left, bool Pressed);
	int ResolveMovementDirection(int Dummy, bool LeftPressed, bool RightPressed, bool ConsumePending);

	// Gores mode
	void UpdateGoresMode();
	void OnFireInput(int Pressed, bool Changed);
	void OnWeaponSelectInput();
	void BindGunKey();
	void BindZeroGravKey();
	void BindZoomKey(bool ZoomIn);

	// Antiping
	void UpdateAntiping();

	// Auto team lock
	void OnChatMessage(int ClientId, int Team, const char *pMessage);
	void LockTeamNow();

	// Team invites
	static constexpr const char *INVITES_FILE = "lazy_invites.txt";

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
	static void ConZeroGravForced(IConsole::IResult *pResult, void *pUserData);
	void BindZeroGravKeyForced();
	void RestoreZeroGravKey();
	void RestoreStaleZeroGravKeyBind();
	static void ConZoomIn(IConsole::IResult *pResult, void *pUserData);
	static void ConZoomOut(IConsole::IResult *pResult, void *pUserData);
	static void ConTeamLock(IConsole::IResult *pResult, void *pUserData);
	static void ConInviteApprove(IConsole::IResult *pResult, void *pUserData);
	static void ConInviteAsk(IConsole::IResult *pResult, void *pUserData);
	static void ConInviteDeny(IConsole::IResult *pResult, void *pUserData);
	void ZoomByConfiguredStep(bool ZoomIn);
	void BindZoomKeyForced(int Index);
	void RestoreZoomKey(int Index);
	void RestoreStaleZoomKeyBind(int Index);
	void UpdateZoomKeyBinds();

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

	// Better zooming
	bool m_ZoomKeysInitialized = false;
	bool m_aZoomKeyBound[2] = {};
	int m_aZoomBoundKey[2] = {};
	char m_aZoomPrevBind[2][128] = {};

	// Snap tap
	int m_aSnapApplied[NUM_DUMMIES] = {};
	int m_aSnapLastPressed[NUM_DUMMIES] = {};
	int64_t m_aSnapLastPressedTime[NUM_DUMMIES] = {};
	// Direction of a tap that was released before a tick could sample it.
	int m_aSnapPending[NUM_DUMMIES] = {};

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

	// Telemetry
	static constexpr int TELEMETRY_FRAME_SAMPLES = 4096;
	static constexpr int TELEMETRY_LOSS_SAMPLES = 5;
	float m_aTelemetryFrameTimes[TELEMETRY_FRAME_SAMPLES] = {};
	float m_aTelemetrySorted[TELEMETRY_FRAME_SAMPLES] = {};
	int m_TelemetryFrameCount = 0;
	int m_TelemetryFrameNext = 0;
	float m_TelemetryStatsTimer = 0.0f;
	float m_TelemetryFpsAvg = 0.0f;
	float m_TelemetryFps1Pct = 0.0f;
	float m_TelemetryFps01Pct = 0.0f;
	float m_TelemetryPing = 0.0f;
	float m_TelemetryJitter = 0.0f;
	float m_TelemetryLastPing = -1.0f;
	float m_TelemetryPacketLoss = 0.0f;
	int m_TelemetrySnapshots = 0;
	float m_TelemetrySnapshotRate = 0.0f;
	uint64_t m_TelemetryPrevRecvVital = 0;
	uint64_t m_TelemetryPrevLostVital = 0;
	uint64_t m_TelemetryPrevResend = 0;
	uint64_t m_aTelemetryLossRecv[TELEMETRY_LOSS_SAMPLES] = {};
	uint64_t m_aTelemetryLossLost[TELEMETRY_LOSS_SAMPLES] = {};
	int m_TelemetryLossIndex = 0;

	// Antipiping
	float m_AntipingSmoothedPing = -1.0f;

	// Auto team lock
	// The client never receives the server's per-team lock flag (SetTeamLock is
	// server-side only), so the state is inferred from the team chat messages
	// the server sends on /lock and /unlock, and from our own join.
	int m_AutoTeamLockTeam = -1;
	bool m_AutoTeamLockPending = false;
	float m_AutoTeamLockSeconds = 0.0f;
	bool m_AutoTeamLockLocked = false;
	void ResetAutoTeamLockState();
	void UpdateAutoTeamLock();
	int CurrentDDRaceTeam() const;

	// Auto kill while frozen
	int m_AutoKillTeam = -1;
	float m_AutoKillSeconds = 0.0f;
	bool m_AutoKillFired = false;
	bool m_AutoKillHasPos = false;
	vec2 m_AutoKillAnchorPos = vec2(0.0f, 0.0f);
	void ResetAutoKillState();
	void UpdateAutoKill();

	// Team invites via whispers
	void HandleTeamInvite(int ClientId, const char *pMessage);
	void AddInviteEntry(const char *pStatus, const char *pName);
	int InviteStatus(const char *pName) const;

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

	// Hooked freeze predictor
	static constexpr int ZERO_GRAV_HISTORY_SIZE = 40;
	float m_ZeroGravCheckTimer = 0.0f;
	bool m_ZeroGravWarning = false;
	float m_ZeroGravSeconds = 0.0f;
	float m_ZeroGravWarningHideTimer = 0.0f;
	int m_ZeroGravFastChecks = 0;
	int m_ZeroGravSpeedrunCooldown = 0;
	int m_ZeroGravMissedChecks = 0;
	vec2 m_ZeroGravHookPos = vec2(0.0f, 0.0f);
	bool m_ZeroGravHasHookPos = false;
	vec2 m_aZeroGravHistory[ZERO_GRAV_HISTORY_SIZE] = {};
	int m_ZeroGravHistoryCount = 0;
	int m_ZeroGravHistoryNext = 0;
	int m_ZeroGravTarget = -1;

	// Forced mode is only a toggle for the ordinary predictor.
	bool m_ZeroGravForcedEnabled = false;
	bool m_ZeroGravForcedStatusOn = false;
	float m_ZeroGravForcedStatusUntil = 0.0f;
	bool m_ZeroGravForcedBindHeld = false;
	bool m_ZeroGravKeyInitialized = false;
	bool m_ZeroGravWasForced = false;
	bool m_ZeroGravKeyBound = false;
	int m_ZeroGravBoundKey = 0;
	char m_aZeroGravPrevBind[256] = {};

	void ResetAutoMarginState();
	void UpdateAutoMargin();
	void ApplyAspect(int Width, int Height);
	void UpdateAspectConfirm();
	void UpdateClientTypes();
	void UpdateClientLogoState();
	void UpdateBestClientList();
	void FinishBestClientList();
	void ResetBestClientList();
	void ResetTelemetry();
	void UpdateTelemetry();
	void RenderTelemetry();
	int FindZeroGravityTarget() const;
	void ClearZeroGravityWarning(bool Immediate);
	void UpdateZeroGravityWarningHideTimer();
	void ResetZeroGravityTracking();
	void ToggleForcedZeroGravity();
	float SimulateZeroGravityFreeze(const CCharacterCore &Core, int Hook, int Direction, int Jump, bool SnapshotHooked, vec2 SnapshotHookPos) const;
	bool AnyForcedZeroGravityBindKeyPressed() const;
	void UpdateZeroGravityPredictor();
	bool PathHitsFreeze(CCollision *pCollision, vec2 PrevPos, vec2 Pos) const;
	void RenderZeroGravityWarning();
	void RenderZeroGravityForcedStatus();
};

#endif
