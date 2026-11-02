// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SDCardLayoutTypes.h"
#include "ShowDownTypes.h"
#include "TimerManager.h"
#include "Templates/Function.h"
#include "ShowDownGameModeBase.generated.h"

class ACard;
class APlayerPawn;
class ASDCardPlacementAnchor;
class ASDPlayerSeat;
class ASDMultiplayerSeatAnchor;
class ASDSelfShotGunActor;
class UCardSystem;
class ACollector;
class UCollectorAISystem;
class UBettingSystem;
class URoundResolver;
class URouletteSystem;
struct FCollectorBetDecision;
struct FSDBetActionPanelState;
struct FSDLLMBossContext;
class AShowDownCharacter;
class AShowDownGameStateBase;
class AController;
class APlayerController;
class ASDPlayerState;
class ALevelSequenceActor;
class ASDBetActionPanelActor;
class ULevelSequence;
class ULevelSequencePlayer;
class USceneComponent;
enum class ESDCardPlacementRole : uint8;

//각 플레이어(콜렉터, 플레이어, 멀티플레이어) 에 대한 값(손패, 이마의 카드, 목숨, 베팅값) 구조체로 저장
USTRUCT(BlueprintType)
struct FShowDownParticipantState
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<ACard*> HandCards;

	UPROPERTY()
	ACard* ForeheadCard = nullptr;

	UPROPERTY()
	int32 Lives = 3;

	UPROPERTY()
	int32 CurrentBet = 0;
};

//스테이지 구조체
USTRUCT(BlueprintType)
struct FShowDownStageRule
{
	GENERATED_BODY()

	//스테이지 시작 체력
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Stage")
	int32 StartingLives = 3;

	//스테이지 최소 베팅 총알 수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Stage")
	int32 MinimumBet = 1;

	//콜렉터 블러핑 확률
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Stage")
	float CollectorBluffRate = 0.15f;

	// 플레이어 이마 카드에 관해 거짓 숫자를 끝까지 주장할 확률
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Stage|Dialogue", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CollectorCardClaimBluffRate = 0.45f;

	// 정확한 숫자를 말하지 않고 회피하는 확률. 남은 확률은 진실 모드다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Stage|Dialogue", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CollectorCardClaimEvasiveRate = 0.25f;

	// 진실/블러프 라운드 중 정확한 숫자를 직접 말할 수 있는 비율
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Stage|Dialogue", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CollectorCardClaimExactRate = 0.50f;

	//콜렉터 공격성
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Stage")
	float CollectorAggression = 0.5f;

	//자신이 7일 때 폴드하면 6발 장전할지 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Stage")
	bool bSevenFoldLoadsSix = true;
};

enum class ECollectorCardClaimMode : uint8
{
	Evasive,
	Honest,
	Bluff
};

UCLASS()
class SHOWDOWN_API AShowDownGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	AShowDownGameModeBase();

	// 덱 관리 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShowDown|Deck")
	UCardSystem* CardSystem;

	//콜렉터 AI 시스템
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShowDown|AI")
	UCollectorAISystem* CollectorAISystem;
	
	// 스폰할 카드 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ShowDown|Card")
	TSubclassOf<ACard> CardClass;

	// 플레이어에게 줄 카드 수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Card")
	int32 HandCount = 5;

	// 카드 사이 간격
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Card")
	float CardSpacing = 70.0f;

	// 카드 위치
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Card")
	float ForwardOffset = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Card")
	float HeightOffset = 65.0f;

	// 카드 각도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Card", meta = (ClampMin = "-45.0", ClampMax = "45.0"))
	float LeanAngle = 0.0f;

	// 같은 줄에서 살짝 앞뒤로 겹치는 정도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Card", meta = (ClampMin = "0.0"))
	float LayerStep = 0.5f;
	
	
	
	// 베팅 시스템
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShowDown|Betting")
	UBettingSystem* BettingSystem;

	// 라운드 판정 시스템
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShowDown|Round")
	URoundResolver* RoundResolver;

	// 룰렛 판정 시스템
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShowDown|Roulette")
	URouletteSystem* RouletteSystem;

	//스테이지별 규칙 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Stage")
	TArray<FShowDownStageRule> StageRules;

	//현재 스테이지 인덱스
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShowDown|Stage")
	int32 CurrentStageIndex = 0;

	// 플레이어가 선택한 카드를 게임모드에 전달
	UFUNCTION(BlueprintCallable, Category = "ShowDown|Card")
	void PlayerSelectedCard(ACard* SelectedCard);

	void PlayerSelectedCardFromController(AController* SubmittingController, ACard* SelectedCard);

	void SetMultiplayerRaisePreviewTarget(ASDPlayerState* SubmittingPlayer, int32 TargetBet);

	// 베팅 단계 시작
	UFUNCTION(BlueprintCallable, Category = "ShowDown|Betting")
	void StartBettingPhase();

	// 플레이어 체크 또는 콜
	UFUNCTION(BlueprintCallable, Category = "ShowDown|Betting")
	void PlayerCheck();

	// 플레이어 레이즈
	UFUNCTION(BlueprintCallable, Category = "ShowDown|Betting")
	void PlayerRaise();

	// 플레이어가 지정한 최종 총알 수로 레이즈
	UFUNCTION(BlueprintCallable, Category = "ShowDown|Betting")
	void PlayerRaiseTo(int32 BulletCount);

	// 플레이어 폴드
	UFUNCTION(BlueprintCallable, Category = "ShowDown|Betting")
	void PlayerFold();

	UFUNCTION(BlueprintCallable, Category = "ShowDown|LLM")
	void SubmitPlayerDialogueInput(const FString& PlayerDialogue);

	UFUNCTION(BlueprintCallable, Category = "ShowDown|Chat")
	void SubmitPlayerDialogueInputFromPlayer(const FString& PlayerDialogue, const FString& SenderName);

	//연출팀이 Phase 연출 종료를 코어에 알릴 때 호출
	UFUNCTION(BlueprintCallable, Category = "ShowDown|Presentation")
	void NotifyPresentationFinished(EShowDownPhase FinishedPhase);

	//연출팀이 Phase 연출 종료를 코어에 알릴 때 호출
	UFUNCTION(BlueprintCallable, Category = "ShowDown|Presentation", meta = (DisplayName = "eventEnd"))
	void EventEnd(EShowDownPhase FinishedPhase);

	// 싱글플레이 한 판을 시작합니다. 콜렉터를 찾고 1스테이지부터 진행합니다.
	// 메인 레벨에서는 싱글플레이 버튼을 눌렀을 때 HubFlowManager가 이 함수를 호출합니다.
	UFUNCTION(BlueprintCallable, Category = "ShowDown|Flow")
	void StartSinglePlayer();

	/** Called by the hub after its local pawn-camera blend really completes. */
	void NotifySinglePlayerGameplayCameraReady();

	UFUNCTION(BlueprintCallable, Category = "ShowDown|Flow")
	void StartMultiplayerGame();

	void RefreshMultiplayerLobbyPlayers();
	bool RequestLobbyKickFromController(AController* RequestingController, const FString& TargetPlayerId);
	void SetMultiplayerVoiceTalking(AController* RequestingController, bool bIsTalking);
	void NotifyInitialCardDealCameraReady(AController* ReadyController);

	// Shared by the multiplayer queue and automation tests: recovery starts only
	// when the gun resolves, so its duration is added after ResultDelay.
	static float CalculateRoulettePresentationFinishDelay(
		float ResultDelay,
		float GunPresentationDelay,
		float HitRecoveryDuration);

	// The final roulette target reserves both its presentation and post-shot hold
	// so EndRound cannot collapse onto the firing frame.
	static float CalculateRouletteProgressionFinishDelay(
		float ResultDelay,
		float PresentationFinishDelay,
		float PostShotHoldDuration);
	static float CalculateRouletteTargetHandoffDelay(
		float ResultDelay,
		float PresentationFinishDelay,
		float PostShotHoldDuration,
		float InterShotDelay,
		bool bHasFollowingTarget);

	// Keeps the next multiplayer turn from overtaking a configurable bullet-load
	// cascade, while retaining the normal action interval for shorter effects.
	static float CalculateRaiseBulletLoadHandoffDelay(
		float BaseActionInterval,
		float PresentationDuration,
		float SafetyPadding = 0.10f);

	void RequestMultiplayerRestartFromController(AController* RequestingController);

	// 게임 종료 후 허브(메인메뉴)로 돌아갈 때 게임판을 정리합니다.
	// 진행 중인 타이머/베팅 상태를 끄고 테이블의 카드를 모두 제거합니다.
	UFUNCTION(BlueprintCallable, Category = "ShowDown|Flow")
	void ResetForHubReturn();

	// true면 BeginPlay에서 곧장 게임을 시작합니다(테스트 레벨용 기본값).
	// 단, 레벨에 HubFlowManager가 있으면 자동 시작을 미루고 허브 흐름이 시작을 제어합니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Flow")
	bool bAutoStartOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Camera", meta = (ClampMin = "0.0"))
	float GameplayCameraLookSensitivity = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Camera")
	float GameplayCameraMinPitch = -35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Camera")
	float GameplayCameraMaxPitch = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Camera")
	float GameplayCameraMinYawOffset = -45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Camera")
	float GameplayCameraMaxYawOffset = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Camera")
	bool bInvertGameplayCameraMouseY = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Camera|Breathing")
	bool bEnableGameplayCameraBreathingSway = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Camera|Breathing", meta = (ClampMin = "0.0"))
	float GameplayCameraBreathingSwaySpeed = 0.38f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Camera|Breathing")
	FRotator GameplayCameraBreathingSwayRotationAmplitude = FRotator(0.12f, 0.05f, 0.08f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Camera|Breathing")
	FVector GameplayCameraBreathingSwayLocationAmplitude = FVector(0.0f, 0.0f, 0.8f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Camera|Breathing", meta = (ClampMin = "0.0"))
	float GameplayCameraBreathingSwayBlendInTime = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation")
	bool bAutoAdvanceRevealWithoutPresentation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation", meta = (ClampMin = "0.0"))
	float RevealAutoAdvanceSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Status", meta = (DisplayName = "Use Bet Status Presentation"))
	bool bUseBetBulletPresentation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions")
	bool bUseBetActionPanel = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions")
	TSubclassOf<ASDBetActionPanelActor> BetActionPanelClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (DisplayName = "Action Panel Hand Forward Offset"))
	float BetActionPanelDistanceFromCenter = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (DisplayName = "Action Panel Hand Right Offset"))
	float BetActionPanelRightOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (DisplayName = "Action Panel Hand Height Offset"))
	float BetActionPanelHeightOffset = 16.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (DisplayName = "Action Panel Rotation Offset"))
	FRotator BetActionPanelRotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (ClampMin = "0.1", ClampMax = "2.0", DisplayName = "Action Panel Visual Scale"))
	float BetActionPanelVisualScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (ClampMin = "1.0", DisplayName = "Button Height"))
	float BetActionButtonHeight = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (ClampMin = "1.0", DisplayName = "Check Call Button Width"))
	float BetActionPrimaryButtonWidth = 46.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (ClampMin = "1.0", DisplayName = "Raise Button Width"))
	float BetActionRaiseButtonWidth = 58.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (ClampMin = "1.0", DisplayName = "Minus Plus Button Width"))
	float BetActionStepButtonWidth = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (ClampMin = "1.0", DisplayName = "Fold Button Width"))
	float BetActionFoldButtonWidth = 46.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (DisplayName = "Raise Row Height"))
	float BetActionRaiseRowHeight = 7.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (DisplayName = "Fold Check Call Row Height"))
	float BetActionPrimaryRowHeight = -7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (ClampMin = "0.1", DisplayName = "Bullet Spacing"))
	float BetActionBulletSpacing = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (ClampMin = "0.001", DisplayName = "Bullet Mesh Scale"))
	float BetActionBulletScale = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions", meta = (DisplayName = "Bullet Row Offset"))
	FVector BetActionBulletRowOffset = FVector(0.0f, 0.0f, 18.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions|Animation", meta = (ClampMin = "0.05", ClampMax = "1.0", DisplayName = "Button Pop Duration"))
	float BetActionButtonAnimationDuration = 0.34f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions|Animation", meta = (ClampMin = "0.0", ClampMax = "0.25", DisplayName = "Button Stagger Delay"))
	float BetActionButtonAnimationStagger = 0.035f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions|Animation", meta = (ClampMin = "0.0", ClampMax = "0.5", DisplayName = "Button Bounce Strength"))
	float BetActionButtonBounceStrength = 0.14f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions|Animation", meta = (ClampMin = "0.05", ClampMax = "1.0", DisplayName = "Bullet Pop Duration"))
	float BetActionBulletAnimationDuration = 0.26f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions|Animation", meta = (ClampMin = "0.0", ClampMax = "0.25", DisplayName = "Bullet Reveal Stagger"))
	float BetActionBulletRevealStagger = 0.055f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Bet Actions|Animation", meta = (ClampMin = "0.0", ClampMax = "0.5", DisplayName = "Bullet Bounce Strength"))
	float BetActionBulletBounceStrength = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Card Reveal")
	bool bUseCardRevealPresentation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Card Reveal", meta = (ClampMin = "0.0"))
	float CardRevealLeadInSeconds = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Card Reveal", meta = (ClampMin = "0.0"))
	float CardRevealStepSeconds = 0.24f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Card Reveal", meta = (ClampMin = "0.0"))
	float CardRevealHoldSeconds = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Card Reveal", meta = (DisplayName = "Single Forward Distance"))
	float CardRevealForwardDistance = 42.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Card Reveal", meta = (DisplayName = "Table Yaw"))
	float CardRevealTableYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Card Reveal", meta = (DisplayName = "Height"))
	float CardRevealHeightOffset = 10.0f;

	// Single-player neighboring-card gap for the linear fallback layout.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Card Reveal", meta = (ClampMin = "0.0", DisplayName = "Reveal Card Spacing"))
	float CardRevealSideSpacing = 10.0f;

	// Desired nearest-neighbor gap for multiplayer cards. This is intentionally
	// separate from the legacy spacing property because existing Blueprint CDOs
	// may carry a much larger authored override.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Card Reveal", meta = (ClampMin = "0.0", DisplayName = "Multiplayer Reveal Card Gap"))
	float MultiplayerCardRevealGap = 20.0f;

	// Kept under a new property name so legacy Blueprint defaults authored at
	// 1.12 cannot silently override the new 1.0 reveal scale.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Card Reveal", meta = (ClampMin = "0.1", DisplayName = "Card Reveal Visual Scale"))
	float CardRevealVisualScaleMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Card Reveal", meta = (DisplayName = "Rotation Offset"))
	FRotator CardRevealRotationOffset = FRotator(-90.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Multiplayer Flow", meta = (ClampMin = "0.0", DisplayName = "Pause Between Bet Actions"))
	float MultiplayerBetActionIntervalSeconds = 0.6f;

	// Single-player and multiplayer share these authored cinematic beats so the
	// same reveal, spotlight and post-shot timing contract drives both flows.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Round Cinematic", meta = (ClampMin = "0.0", DisplayName = "Bet Focus Hold"))
	float RoundCinematicBetFocusHoldSeconds = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Round Cinematic", meta = (ClampMin = "0.0", DisplayName = "Final Bet To Blackout"))
	float RoundCinematicFinalBetToBlackoutSeconds = 2.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Round Cinematic", meta = (ClampMin = "0.0", DisplayName = "Collector Turn Lead In"))
	float RoundCinematicCollectorTurnLeadInSeconds = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Round Cinematic", meta = (ClampMin = "0.0", DisplayName = "Blackout To Table Spotlight"))
	float RoundCinematicBlackoutToTableSpotlightSeconds = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Round Cinematic", meta = (ClampMin = "0.0", DisplayName = "Table Spotlight To Reveal"))
	float RoundCinematicTableSpotlightToRevealSeconds = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Round Cinematic", meta = (ClampMin = "0.0", DisplayName = "Reveal To Loser Spotlight"))
	float RoundCinematicRevealToLoserSpotlightSeconds = 2.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Round Cinematic", meta = (ClampMin = "0.0", DisplayName = "Loser Spotlight Hold"))
	float RoundCinematicLoserSpotlightHoldSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Round Cinematic", meta = (ClampMin = "0.0", DisplayName = "Post Shot Progress Hold"))
	float RoundCinematicPostShotProgressHoldSeconds = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Round Cinematic", meta = (ClampMin = "0.0", DisplayName = "Sequential Shot Handoff"))
	float MultiplayerRouletteInterShotDelaySeconds = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Initial Deal", meta = (DisplayName = "Use Initial Card Deal Presentation"))
	bool bUseInitialCardDealPresentation = true;

	// GameMode defaults cannot safely hold a direct reference to an actor in a
	// level. Tag the placed deck actor with this value to select it explicitly;
	// the authored deck mesh is detected automatically when no tag is present.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Initial Deal", meta = (DisplayName = "Deck Source Actor Tag"))
	FName InitialDealDeckSourceActorTag = TEXT("InitialDealDeckSource");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Initial Deal", meta = (ClampMin = "0.0", ClampMax = "4.0", DisplayName = "Flat Card Overlap Step"))
	float InitialDealFlatCardSpacing = 4.0f;

	/** How early movement targets are replicated so clients have them before the visual start frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Initial Deal", meta = (ClampMin = "0.0", ClampMax = "0.5", DisplayName = "Network Schedule Lead Time"))
	float InitialDealNetworkScheduleLeadTime = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Initial Deal", meta = (ClampMin = "0.1", ClampMax = "2.0", DisplayName = "Card Move Duration"))
	float InitialDealCardMoveDuration = 0.70f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Initial Deal", meta = (ClampMin = "0.0", ClampMax = "60.0", DisplayName = "Hand Move Arc Height"))
	float InitialDealHandMoveArcHeight = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Initial Deal", meta = (ClampMin = "0.1", ClampMax = "2.0", DisplayName = "Hand Move Duration"))
	float InitialDealHandMoveDuration = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Initial Deal", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Card Bounce Strength"))
	float InitialDealCardBounceStrength = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Initial Deal", meta = (ClampMin = "0.0", ClampMax = "3.0", DisplayName = "Beat Delay"))
	float InitialDealBeatDelay = 0.80f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Card Selection", meta = (ClampMin = "0.0", DisplayName = "Collector Card Selection Delay"))
	float CollectorCardSelectionDelaySeconds = 2.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Initial Deal", meta = (ClampMin = "0.0", ClampMax = "5.0", DisplayName = "Showcase Hold Duration"))
	float InitialDealShowcaseHoldDuration = 1.50f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Initial Deal", meta = (ClampMin = "10.0", DisplayName = "Showcase Grid Spacing (Column, Row)"))
	FVector2D InitialDealShowcaseGridSpacing = FVector2D(10.0f, 12.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Single Player Intro")
	bool bPlaySinglePlayerIntro = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Single Player Intro", meta = (EditCondition = "bPlaySinglePlayerIntro"))
	TObjectPtr<ULevelSequence> SinglePlayerIntroSequence = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Single Player Intro", meta = (EditCondition = "bPlaySinglePlayerIntro"))
	FName SinglePlayerIntroPlayerBindingTag = TEXT("Player");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Single Player Intro", meta = (EditCondition = "bPlaySinglePlayerIntro"))
	FName SinglePlayerIntroCollectorBindingTag = TEXT("Collector");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Single Player Intro", meta = (EditCondition = "bPlaySinglePlayerIntro"))
	bool bUseSinglePlayerIntroFallbackWhenNoSequence = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Single Player Intro", meta = (EditCondition = "bPlaySinglePlayerIntro && bUseSinglePlayerIntroFallbackWhenNoSequence", ClampMin = "0.0"))
	float SinglePlayerIntroFallbackDuration = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Presentation|Single Player Intro", meta = (EditCondition = "bPlaySinglePlayerIntro && bUseSinglePlayerIntroFallbackWhenNoSequence", ClampMin = "0.0"))
	float SinglePlayerIntroFallbackStartDistance = 280.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Chat", meta = (DisplayName = "Single Player Opening Greeting"))
	FString SinglePlayerOpeningGreeting = TEXT("왔네. 한 판 제대로 해보자.");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Decision Timer", meta = (ClampMin = "1.0", DisplayName = "Card Selection Time Limit"))
	float CardSelectionTimeLimitSeconds = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Decision Timer", meta = (ClampMin = "1.0", DisplayName = "Betting Turn Time Limit"))
	float BettingTurnTimeLimitSeconds = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShowDown|Debug")
	bool bShowGameFlowDebugMessages = false;

	UFUNCTION(BlueprintCallable, Category = "ShowDown|Betting")
	void RequestPlayerBetAction(EShowDownBetAction Action, int32 TargetBet);

	void RequestPlayerBetActionFromController(AController* SubmittingController, EShowDownBetAction Action, int32 TargetBet);


protected:
	virtual void BeginPlay() override;
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

private:
	//플레이어 스탯
	UPROPERTY()
	FShowDownParticipantState PlayerState;

	//콜렉터 스탯
	UPROPERTY()
	FShowDownParticipantState CollectorState;
	
	bool bBettingPhase = false;
	FTimerHandle RevealDelayHandle;
	FTimerHandle DecisionTimeoutTimerHandle;
	FTimerHandle MultiplayerRevealContinuationTimerHandle;
	TArray<FTimerHandle> CardRevealPresentationTimerHandles;
	TArray<FTimerHandle> MultiplayerRoundTimerHandles;
	TArray<FTimerHandle> SingleRoundCinematicTimerHandles;
	TArray<FTimerHandle> InitialCardDealPresentationTimerHandles;
	int32 BettingRaisesLeft = 6;
	bool bHasLastRaiser = false;
	EShowDownSide LastRaiser = EShowDownSide::Player;
	EShowDownSide CurrentRoundFirstSide = EShowDownSide::Player;
	EShowDownSide NextRoundFirstSide = EShowDownSide::Player;
	bool bPlayerHasActedInBetting = false;
	bool bCollectorHasActedInBetting = false;
	bool bCollectorBetDecisionInProgress = false;
	bool bSingleBetTransitionInProgress = false;
	bool bCollectorTurnLeadInProgress = false;
	bool bCollectorCardSelectionPending = false;
	bool bPlayerCardSelectionPresentationComplete = false;
	bool bCollectorCardSelectionPresentationComplete = false;
	bool bHasPendingRoundReveal = false;
	bool bHasPendingFoldReveal = false;
	bool bCollectorActionPresentationInProgress = false;
	EShowDownRoundResult PendingRoundResult = EShowDownRoundResult::Draw;
	EShowDownSide PendingFoldedSide = EShowDownSide::Player;
	int32 PendingFoldLoadCount = 1;
	FTimerHandle CollectorActionPresentationTimerHandle;
	FTimerHandle SelfShotHitRecoveryWaitTimerHandle;
	TFunction<void()> CollectorActionPresentationContinuation;
	TFunction<void()> SelfShotGunResultContinuation;
	TFunction<void()> SelfShotGunPresentationContinuation;
	TFunction<void()> MultiplayerGunResultContinuation;
	TArray<TFunction<void()>> QueuedCollectorActionPresentationContinuations;
	FString LatestPlayerDialogueInput;
	FString RecentDialogueHistory;
	FString RecentRoundHistory;
	FString CurrentRoundActionHistory;
	FString DiscardedCardsSummary;
	FString PendingBossChatReplyDialogue;
	int32 CurrentRoundPlayerGaveRank = 0;
	int32 CurrentRoundCollectorGaveRank = 0;
	ECollectorCardClaimMode CurrentCollectorCardClaimMode = ECollectorCardClaimMode::Evasive;
	int32 CurrentCollectorClaimedPlayerRank = 0;
	bool bCurrentCollectorExactClaimAllowed = false;
	int32 LastRoundPlayerCardRank = 0;
	int32 LastRoundCollectorCardRank = 0;
	bool bCurrentRoundSummaryRecorded = false;
	bool bBossChatReplyInFlight = false;
	bool bHasPendingBossChatReply = false;
	float LastBossChatReplyRequestTime = -1000.0f;
	FTimerHandle BossChatReplyRetryTimerHandle;

	UPROPERTY()
	TArray<TObjectPtr<ASDPlayerState>> MultiplayerPlayers;

	UPROPERTY()
	TArray<TObjectPtr<ASDPlayerState>> MultiplayerEliminationOrder;

	UPROPERTY()
	TObjectPtr<ASDPlayerState> MultiplayerDuelA = nullptr;

	UPROPERTY()
	TObjectPtr<ASDPlayerState> MultiplayerDuelB = nullptr;

	UPROPERTY()
	TObjectPtr<ASDPlayerState> MultiplayerCurrentBetter = nullptr;

	UPROPERTY()
	TObjectPtr<ASDPlayerState> MultiplayerLastCheckedPlayer = nullptr;

	// The player who lost the previous round leads the next multiplayer round.
	// A draw keeps the current lead; an eliminated loser falls back to the next survivor.
	UPROPERTY()
	EShowDownPlayerSlot MultiplayerNextFirstSlot = EShowDownPlayerSlot::None;

	UPROPERTY()
	TArray<TObjectPtr<ASDMultiplayerSeatAnchor>> MultiplayerSeatAnchors;

	UPROPERTY()
	TObjectPtr<ASDPlayerState> MultiplayerRoundLeader = nullptr;

	UPROPERTY()
	TSet<TObjectPtr<ASDPlayerState>> MultiplayerFoldedPlayers;

	TMap<EShowDownPlayerSlot, int32> MultiplayerFoldLoadCounts;

	UPROPERTY()
	TSet<TObjectPtr<ASDPlayerState>> MultiplayerPlayersActed;

	UPROPERTY()
	TSet<TObjectPtr<ASDPlayerState>> MultiplayerRestartVotes;

	FTimerHandle MultiplayerStartTimerHandle;
	FTimerHandle MultiplayerGunResultFallbackTimerHandle;
	double MultiplayerStartDeadlineSeconds = 0.0;
	bool bMultiplayerMatchStarted = false;
	bool bMultiplayerRoundResolving = false;
	bool bMultiplayerBetTransitionInProgress = false;
	int32 MultiplayerMatchSequence = 0;
	uint32 MultiplayerRoundSequence = 0;
	double MultiplayerRoundProgressBlockedUntilSeconds = 0.0;
	TWeakObjectPtr<ASDPlayerState> MultiplayerFoldPresentationTarget;
	EShowDownPlayerSlot MultiplayerFoldPresentationSlot = EShowDownPlayerSlot::None;
	int32 MultiplayerFoldPresentationTableBet = 0;
	
	//콜렉터 추적
	UPROPERTY()
	ACollector* Collector = nullptr;

	UPROPERTY()
	TObjectPtr<ASDSelfShotGunActor> ActiveSelfShotGunActor = nullptr;
	TWeakObjectPtr<ASDSelfShotGunActor> MultiplayerResultGunActor;
	UPROPERTY()
	TObjectPtr<ASDBetActionPanelActor> BetActionPanelActor = nullptr;

	bool bSelfShotGunPresentationInProgress = false;
	bool bPendingSelfShotRouletteResult = false;
	bool bPendingSelfShotLiveRound = false;
	EShowDownSide PendingSelfShotTargetSide = EShowDownSide::Player;
	uint8 PendingSelfShotTriggerTargetMask = 0;
	uint8 PendingMultiplayerTriggerTargetMask = 0;
	double SingleRoundProgressBlockedUntilSeconds = 0.0;
	double SingleBetFocusStartedAtSeconds = -1.0;
	int32 BetActionPanelRevision = 0;
	int32 BetActionPanelSelectedRaiseTarget = 1;
	EShowDownPlayerSlot BetActionPanelPreviewTurnSlot = EShowDownPlayerSlot::None;
	int32 MultiplayerLiveRoundCount = 0;
	int32 MultiplayerRemainingChamberCount = 6;
	int32 MultiplayerSharedChamberIndex = 0;
	TArray<bool> MultiplayerSharedChambers;
	int32 SingleRouletteLiveRoundCount = 0;
	int32 SingleRemainingChamberCount = 6;
	bool bHasBetBulletAction = false;
	bool bBetBulletActionIsMultiplayer = false;
	EShowDownSide BetBulletActionSide = EShowDownSide::Player;
	EShowDownPlayerSlot BetBulletActionSlot = EShowDownPlayerSlot::None;
	EShowDownBetAction BetBulletAction = EShowDownBetAction::Check;
	TMap<EShowDownSide, EShowDownBetAction> SingleLastBetActions;
	TMap<EShowDownPlayerSlot, EShowDownBetAction> MultiplayerLastBetActions;
	bool bHasBetBulletRouletteTarget = false;
	bool bBetBulletRouletteTargetIsMultiplayer = false;
	EShowDownSide BetBulletRouletteTargetSide = EShowDownSide::Player;
	EShowDownPlayerSlot BetBulletRouletteTargetSlot = EShowDownPlayerSlot::None;
	int32 BetBulletRouletteBulletCount = 0;

	UPROPERTY()
	TObjectPtr<ULevelSequencePlayer> ActiveSinglePlayerIntroSequencePlayer = nullptr;

	UPROPERTY()
	TObjectPtr<ALevelSequenceActor> ActiveSinglePlayerIntroSequenceActor = nullptr;

	UPROPERTY()
	TObjectPtr<AShowDownCharacter> SinglePlayerIntroPlayerCharacter = nullptr;

	UPROPERTY()
	TObjectPtr<AShowDownCharacter> SinglePlayerIntroCollectorCharacter = nullptr;

	FTimerHandle SinglePlayerIntroFallbackTimerHandle;
	FTransform SinglePlayerIntroPlayerStartTransform;
	FTransform SinglePlayerIntroPlayerTargetTransform;
	FTransform SinglePlayerIntroCollectorStartTransform;
	FTransform SinglePlayerIntroCollectorTargetTransform;
	float SinglePlayerIntroFallbackStartTime = 0.0f;
	bool bSinglePlayerIntroFallbackActive = false;
	bool bSinglePlayerMatchStarted = false;
	bool bSinglePlayerGameplayCameraReady = false;
	bool bSinglePlayerStageReadyForMatchIntro = false;
	bool bSinglePlayerMatchIntroQueued = false;
	bool bSinglePlayerOpeningGreetingSent = false;
	bool bInitialCardDealPresentationInProgress = false;
	bool bInitialCardDealPresentationPlayed = false;
	bool bInitialDealCinematicCueActive = false;
	bool bInitialCardDealIsMultiplayer = false;
	int32 InitialCardDealDeckCopies = 2;
	// Number of rank copies in the current logical deck cycle. The deck starts
	// with seven cards per participant and is only rebuilt after it is exhausted.
	int32 ActiveCardDeckCopies = 0;
	TFunction<void()> InitialCardDealPresentationContinuation;
	mutable bool bInitialCardDeckBoundsCacheValid = false;
	mutable FVector CachedInitialCardDeckTop = FVector::ZeroVector;
	mutable float CachedInitialCardDeckBottomZ = 0.0f;
	mutable bool bInitialCardTableSurfaceCacheValid = false;
	mutable float CachedInitialCardTableSurfaceZ = 0.0f;
	mutable float CachedInitialCardShowcasePadRadius = 0.0f;
	mutable TArray<FVector> CachedInitialCardFlatSlotCenters;
	mutable bool bInitialCardSpatialCacheValid = false;
	mutable FVector CachedInitialCardTableCenter = FVector::ZeroVector;
	mutable FVector CachedInitialCardShowcaseCenter = FVector::ZeroVector;
	mutable float CachedInitialCardShowcasePlaneZ = 0.0f;
	mutable TWeakObjectPtr<USceneComponent> CachedInitialCardReferenceHandSlot;
	TSet<EShowDownPlayerSlot> InitialCardDealCameraReadySlots;
	bool bInitialCardDealShowcaseStarted = false;

	UPROPERTY()
	TArray<TObjectPtr<ACard>> InitialCardDealDeckCards;

	mutable bool bCardPlacementAnchorCacheInitialized = false;
	mutable TMap<ESDCardPlacementRole, TWeakObjectPtr<ASDCardPlacementAnchor>> CachedCardPlacementAnchors;
	mutable bool bPlayerSeatCacheInitialized = false;
	mutable TMap<EShowDownSide, TWeakObjectPtr<ASDPlayerSeat>> CachedPlayerSeats;
	mutable TWeakObjectPtr<ASDPlayerSeat> CachedFirstPlayerSeat;

	UFUNCTION()
	void HandleSelfShotGunPresentationFinished();

	UFUNCTION()
	void HandleSelfShotGunShotResolved();

	UFUNCTION()
	void HandleSelfShotGunTriggerPullStarted();

	UFUNCTION()
	void HandleSinglePlayerIntroSequenceFinished();

	// 덱을 만들고 섞은 뒤에 플레이어와 콜렉터에게 5장 스폰
	void DealInitialHand();
	void FindCollector();
	void QueueCollectorGiveCardToPlayer();
	void CollectorGiveCardToPlayer();
	void TryStartBettingAfterCardSelections();
	float EstimateCollectorWinChance() const;
	void ResolveCollectorBetResponse();
	void QueueCollectorBetResponseAfterFocus();
	void HoldSingleBetFocusThen(EShowDownSide ActingSide, TFunction<void()>&& Continuation);
	void ScheduleSingleRoundCinematicAction(float DelaySeconds, TFunction<void()>&& Action);
	void ClearSingleRoundCinematicTimers();
	void StartSinglePreRevealSequence(TFunction<void()>&& RevealAction);
	void BeginSingleRoundReveal();
	void BeginSingleFoldReveal(EShowDownSide FoldedSide, int32 LoadCount);
	void BroadcastTableCinematicCue(ESDTableCinematicCue Cue, uint8 TargetMask = 0) const;
	void SetInitialDealCinematicCueActive(bool bActive);
	FCollectorBetDecision SanitizeCollectorDecision(const FCollectorBetDecision& RawDecision) const;
	void ExecuteCollectorBetDecision(const FCollectorBetDecision& CollectorDecision, int32 GivenCardRank);
	FSDLLMBossContext BuildLLMBossContext(int32 CurrentBet, int32 GivenCardRank) const;
	FSDLLMBossContext BuildLLMChatContext(const FString& PlayerDialogue) const;
	void TryRequestBossChatReply(const FString& PlayerDialogue, bool bIgnoreCooldown = false);
	void RequestPendingBossChatReply();
	void QueueSinglePlayerOpeningGreeting();
	void AppendRecentDialogueLine(const FString& Speaker, const FString& Message);
	void ResetCurrentRoundMemory();
	void InitializeCollectorCardClaimState(int32 ActualPlayerRank);
	FString GetCollectorCardClaimModeText() const;
	FString GetCollectorCardClaimDetailText() const;
	void RecordCurrentRoundAction(const FString& ActionText);
	void AppendRecentRoundSummary(EShowDownRoundResult Result, const FString& Reason);
	FString GetSideText(EShowDownSide Side) const;
	FString GetSideDisplayText(EShowDownSide Side) const;
	FString GetRoundResultText(EShowDownRoundResult Result) const;
	void StartDecisionTimer(
		EShowDownDecisionTimerKind Kind,
		float DurationSeconds,
		EShowDownSide TargetSide,
		EShowDownPlayerSlot TargetSlot);
	void ClearDecisionTimer();
	void HandleDecisionTimerExpired(
		int32 ExpectedRevision,
		EShowDownDecisionTimerKind ExpectedKind,
		EShowDownSide ExpectedSide,
		EShowDownPlayerSlot ExpectedSlot,
		int32 ExpectedRound,
		uint32 ExpectedMultiplayerRoundSequence);
	void HandleSinglePlayerCardSelectionTimeout();
	void HandleMultiplayerCardSelectionTimeout();
	void HandleSinglePlayerBettingTimeout();
	void HandleMultiplayerBettingTimeout(EShowDownPlayerSlot ExpectedSlot);
	void BeginCardSelectionRound();
	void SetNextRoundFirstSideFromResult(EShowDownRoundResult Result);
	void FinishBettingAndResolveRound();
	void BroadcastBossResultReaction(EShowDownRoundResult Result);
	void ResolveFold(EShowDownSide FoldedSide);
	void ContinueRoundAfterReveal(EShowDownRoundResult Result);
	void ContinueFoldAfterReveal(EShowDownSide FoldedSide, int32 LoadCount);
	void ClearBetBulletPresentation();
	void RefreshBetBulletPresentation();
	void PopulateBetActionPanelLayoutState(FSDBetActionPanelState& PanelState) const;
	void ClearBetActionPanel();
	void RefreshBetActionPanel();
	ASDBetActionPanelActor* EnsureBetActionPanelActor();
	FTransform BuildBetActionPanelTransformForSide(EShowDownSide Side) const;
	FTransform BuildBetActionPanelTransformForPlayer(const ASDPlayerState* Player) const;
	FTransform BuildBetActionPanelTransformFromLocation(const FVector& SourceLocation, int32 FallbackOrderIndex) const;
	void RecordSingleBetBulletAction(EShowDownSide Side, EShowDownBetAction Action);
	void RecordMultiplayerBetBulletAction(ASDPlayerState* Player, EShowDownBetAction Action);
	void MarkSingleBetBulletRouletteTarget(EShowDownSide TargetSide, int32 BulletCount);
	void ConsumeSingleRouletteChamber(bool bLiveRound);
	void MarkMultiplayerBetBulletRouletteTarget(ASDPlayerState* TargetPlayer, int32 BulletCount);
	void ClearBetBulletTransientState();
	void ClearBetBulletActionHistory();
	FString BuildBetBulletActionText(EShowDownBetAction Action) const;
	float PlaySinglePlayerCardRevealPresentation();
	float PlayMultiplayerCardRevealPresentation(const TArray<ASDPlayerState*>& RevealedPlayers);
	float PlayCardRevealPresentation(const TArray<ACard*>& Cards);
	void ClearCardRevealPresentationTimers();
	void ClearMultiplayerRoundTimers();
	FTransform BuildCardRevealPresentationTransform(
		ACard* Card,
		int32 CardIndex,
		int32 CardCount,
		float MultiplayerCenterDistance) const;
	bool TryResolveMultiplayerRevealSeatLocation(const ACard* Card, FVector& OutSeatLocation) const;
	void ApplyRouletteResult(EShowDownSide TargetSide, int32 BulletCount, TFunction<void()>&& Continuation);
	void EndRound();
	void ClearForeheadCards();
	void ClearHandCards();
	void SetPlayerHandSelectable(bool bSelectable);
	void WaitForCardPlacementThen(ACard* Card, TFunction<void()>&& Continuation);
	void PlayCollectorActionPresentation();
	void PlayCollectorActionPresentationThen(TFunction<void()>&& Continuation);
	void FinishCollectorActionPresentation();
	void BroadcastCardSelectedAction(EShowDownSide Side) const;
	void BroadcastBetActionCommitted(
		EShowDownSide Side,
		EShowDownBetAction Action,
		int32 TargetBet,
		int32 RaiseAmount = 0,
		bool bWasAutomatic = false) const;
	void BroadcastMultiplayerCardSelectedAction(ASDPlayerState* Player) const;
	void BroadcastMultiplayerBetActionCommitted(
		ASDPlayerState* Player,
		EShowDownBetAction Action,
		int32 TargetBet,
		int32 RaiseAmount = 0,
		bool bWasAutomatic = false) const;
	void BroadcastSystemChatMessage(const FString& Message) const;
	void PlaySelfShotGunPresentationThen(
		EShowDownSide TargetSide,
		bool bLiveRound,
		TFunction<void()>&& ResultContinuation,
		TFunction<void()>&& PresentationContinuation);
	void FinishSelfShotGunPresentation();
	void ResolvePendingSelfShotGunResult();
	void BroadcastPendingSelfShotRouletteResult();
	void ArmMultiplayerGunResult(
		ASDSelfShotGunActor* GunActor,
		float FallbackDelay,
		TFunction<void()>&& ResultContinuation);
	void ResolvePendingMultiplayerGunResult();
	void ClearPendingMultiplayerGunResult();

	UFUNCTION()
	void HandleMultiplayerGunShotResolved();

	UFUNCTION()
	void HandleMultiplayerGunTriggerPullStarted();

	UFUNCTION()
	void HandleMultiplayerGunPresentationFinished();
	ASDSelfShotGunActor* FindSelfShotGunActor() const;
	AShowDownCharacter* FindSingleRouletteCharacter(EShowDownSide TargetSide) const;
	void PlaySinglePlayerIntroThenStartStage();
	bool PlaySinglePlayerIntroSequence(AShowDownCharacter* PlayerCharacter, AShowDownCharacter* CollectorCharacter);
	bool PlaySinglePlayerIntroFallback(AShowDownCharacter* PlayerCharacter, AShowDownCharacter* CollectorCharacter);
	void UpdateSinglePlayerIntroFallback();
	void FinishSinglePlayerIntro();
	void TryQueueSinglePlayerMatchIntro();
	void StartInitialCardDealPresentation(int32 DeckCopies, bool bMultiplayer, TFunction<void()>&& Continuation);
	void StartHandRedealPresentation(bool bMultiplayer, TFunction<void()>&& Continuation);
	void BeginInitialCardDeckShowcase();
	bool TryImmediateInitialCardDealFallback();
	void StopInitialCardDealOnFailure(const TCHAR* Reason);
	void SetInitialCardDealInputLocked(bool bLocked) const;
	void SetInitialDealDeckVisual(int32 RemainingCards, int32 TotalCards) const;
	void RefreshInitialCardDealSpatialCache() const;
	void StartInitialCardDealFromStack();
	bool PrepareSinglePlayerOpeningHands(
		TArray<ACard*>& OutCardsInDealOrder,
		TArray<FTransform>& OutFlatTransforms,
		TArray<FTransform>& OutFinalTransforms,
		int32& OutParticipantCount);
	bool PrepareMultiplayerOpeningHands(
		TArray<ACard*>& OutCardsInDealOrder,
		TArray<FTransform>& OutFlatTransforms,
		TArray<FTransform>& OutFinalTransforms,
		int32& OutParticipantCount);
	bool PrepareSinglePlayerRedealHands(
		TArray<ACard*>& OutCardsInDealOrder,
		TArray<FTransform>& OutFlatTransforms,
		TArray<FTransform>& OutFinalTransforms,
		int32& OutParticipantCount,
		int32& OutDeckRemainingBeforeDeal);
	bool PrepareMultiplayerRedealHands(
		TArray<ACard*>& OutCardsInDealOrder,
		TArray<FTransform>& OutFlatTransforms,
		TArray<FTransform>& OutFinalTransforms,
		int32& OutParticipantCount,
		int32& OutDeckRemainingBeforeDeal);
	void AnimatePreparedOpeningHands(
		const TArray<ACard*>& CardsInDealOrder,
		const TArray<FTransform>& FlatTransforms,
		const TArray<FTransform>& FinalTransforms,
		int32 ParticipantCount,
		int32 DeckRemainingBeforeDeal);
	void FinishInitialCardDealPresentation();
	void ClearInitialCardDealPresentation(bool bDestroyDeckCards = true);
	void ScheduleInitialCardDealAction(float DelaySeconds, TFunction<void()>&& Action);
	FTransform BuildInitialCardGridTransform(int32 CardIndex, int32 DeckCopies, bool bFaceDown) const;
	FTransform BuildInitialCardStackTransform(float DeckHeightAlpha) const;
	FTransform BuildInitialFlatCardTransform(USceneComponent* HandSlot, int32 CardIndex, int32 CardCount) const;
	FQuat BuildInitialFlatCardRotation(const FVector& TowardTableCenter, bool bFaceDown) const;
	FVector ResolveInitialCardDeckTop() const;
	float ResolveInitialCardTableSurfaceZ() const;
	TArray<AShowDownCharacter*> GetShowDownCharacters() const;
	void ConfigureSinglePlayerCharacters();
	void ConfigureMultiplayerCharacters(const TArray<ASDPlayerState*>& Players);

	// 싱글플레이 상대 캐릭터 스킨을 무작위로 고릅니다(플레이어/직전 판과 겹치지 않게).
	static FString PickRandomOpponentSkinId(const FString& PlayerSkinId);
	void RefreshMultiplayerCharacterVisibility();
	float ResolveMultiplayerRouletteResultDelay() const;
	float ResolveMultiplayerRoulettePresentationDelay(bool bLiveRound) const;
	FSDCardHandLayoutSettings GetDefaultHandLayoutSettings() const;
	FSDCardHandLayoutSettings ResolveHandLayoutSettings(EShowDownSide Side) const;
	void ApplyCardMotionForSide(EShowDownSide Side, const TArray<ACard*>& Cards) const;
	void ReflowHandCards(EShowDownSide Side);
	void RefreshNetworkPlayerSlots();
	EShowDownPlayerSlot FindNextOpenPlayerSlot() const;
	void TryStartMultiplayerMatch();
	void StartMultiplayerMatch(const TArray<ASDPlayerState*>& Players);
	TArray<ASDPlayerState*> GetConnectedShowDownPlayers() const;
	bool EvaluateMultiplayerRestartVotes(const TArray<ASDPlayerState*>& EligiblePlayers);
	ASDPlayerState* GetPlayerStateForController(AController* Controller) const;
	void EnsureMultiplayerSeatAnchors();
	void EnsureMultiplayerPawns();
	FTransform GetMultiplayerPawnSpawnTransform(AController* Controller, int32 PlayerIndex);
	ASDPlayerState* FindNextAliveMultiplayerPlayer(ASDPlayerState* AfterPlayer) const;
	ASDPlayerState* FindNextActiveMultiplayerPlayerAfterSlot(EShowDownPlayerSlot AfterSlot) const;
	ASDPlayerState* ResolveNextMultiplayerRoundLeader(
		EShowDownPlayerSlot PreferredLeaderSlot,
		EShowDownPlayerSlot CurrentLeaderSlot) const;
	ASDPlayerState* GetMultiplayerOpponent(ASDPlayerState* Player) const;
	void DealMultiplayerHands();
	void ConfigureMultiplayerHandCardPrivacy(ASDPlayerState* HandOwner, ACard* Card);
	void ConfigureMultiplayerForeheadCardPrivacy(ASDPlayerState* HiddenPlayer, ACard* Card);
	void ForgetMultiplayerCardRank(ACard* Card);
	void ClearMultiplayerHands();
	void RetireEliminatedMultiplayerHands();
	void ClearMultiplayerForeheadCards();
	void ClearLooseMultiplayerCards();
	void StartMultiplayerDuel(ASDPlayerState* FirstPlayer, ASDPlayerState* SecondPlayer);
	bool AreAllAliveMultiplayerPlayersReadyToReveal() const;
	bool AreAllActiveMultiplayerPlayersDoneBetting(int32 CurrentBet) const;
	int32 CountActiveMultiplayerPlayers() const;
	void HandleMultiplayerPlayerDisconnected(ASDPlayerState* LeavingPlayer);
	void StartMultiplayerCardSelection();
	void HandleMultiplayerSelectedCard(ASDPlayerState* SubmittingPlayer, ACard* SelectedCard);
	void StartMultiplayerBetting();
	void HandleMultiplayerBetAction(
		ASDPlayerState* SubmittingPlayer,
		EShowDownBetAction Action,
		int32 TargetBet,
		bool bWasAutomatic = false);
	void ScheduleMultiplayerRoundAction(float DelaySeconds, TFunction<void()>&& Action);
	void QueueNextMultiplayerBetTurn(EShowDownPlayerSlot ActingSlot, int32 CurrentBet, float DelaySeconds);
	void QueueMultiplayerRevealAfterBetting(float CompletedBetFocusHoldSeconds = 0.0f);
	void BeginMultiplayerFoldReveal(
		ASDPlayerState* FoldedPlayer,
		EShowDownPlayerSlot FoldedSlot,
		int32 TableBet,
		int32 FoldLoadCount);
	void CompleteMultiplayerFoldResolution(
		ASDPlayerState* FoldedPlayer,
		EShowDownPlayerSlot FoldedSlot,
		int32 TableBet);
	void RetireMultiplayerFoldedCard(ASDPlayerState* FoldedPlayer);
	void FinishMultiplayerRoundByReveal();
	void ContinueMultiplayerRoundAfterReveal(
		TArray<ASDPlayerState*> RevealedPlayers,
		bool bLoserSpotlightShown = false);
	int32 ResolveMultiplayerFoldLoadCount(const ASDPlayerState* FoldedPlayer) const;
	void PrepareMultiplayerFoldCylinder(int32 LoadCount);
	void InitializeMultiplayerSharedChambers();
	bool ResolveNextMultiplayerSharedChamber();
	void PlayMultiplayerRouletteTargetsSequentially(
		TArray<TWeakObjectPtr<ASDPlayerState>> Targets,
		int32 TargetIndex = 0);
	void RefreshCentralGunStatus();
	float PlayCentralGunRaiseBulletLoadPresentation(
		int32 PreviousBet,
		int32 NewBet,
		EShowDownPlayerSlot SourceSlot);
	float ApplyMultiplayerRoulette(
		ASDPlayerState* TargetPlayer,
		int32 BulletCount,
		float StartDelay = 0.0f,
		bool bUseSharedChambers = false,
		bool bAllowFastFollowUp = false,
		TSharedPtr<TFunction<void()>> FastFollowUpContinuation = nullptr);
	void EndMultiplayerRound();
	void ShowMultiplayerFinalRanking(ASDPlayerState* Winner);
	void SetMultiplayerSelectableHand(ASDPlayerState* Player);
	void SetMultiplayerAliveHandsSelectable(bool bSelectable);
	void ReflowMultiplayerHand(ASDPlayerState* Player);
	USceneComponent* GetHandSlotForPlayerState(ASDPlayerState* Player) const;
	USceneComponent* GetHeadSlotForPlayerState(ASDPlayerState* Player) const;
	FRotator GetForeheadCardRotationOffsetForPlayerState(const ASDPlayerState* Player, const USceneComponent* HeadSlot) const;
	FRotator GetForeheadCardRotationOffsetForSide(EShowDownSide Side, const USceneComponent* HeadSlot) const;
	void NotifyMultiplayerStatus(const FString& Message) const;
	void NotifyMultiplayerPlayerStatus(const ASDPlayerState* Player, const FString& Message) const;
	void StartStage(int32 StageIndex);
	void AdvanceStage();
	const FShowDownStageRule* GetCurrentStageRule() const;
	AShowDownGameStateBase* GetShowDownGameState() const;
	APlayerPawn* GetPrimaryPlayerPawn() const;
	void RefreshCardPlacementAnchorCache() const;
	void RefreshPlayerSeatCache() const;
	ASDCardPlacementAnchor* GetCardPlacementAnchorByRole(ESDCardPlacementRole TargetRole) const;
	ASDCardPlacementAnchor* GetCardPlacementAnchor(EShowDownSide Side, bool bForeheadSlot) const;
	ASDCardPlacementAnchor* GetHandAnchorForSide(EShowDownSide Side) const;
	ASDCardPlacementAnchor* GetForeheadAnchorForSide(EShowDownSide Side) const;
	ASDCardPlacementAnchor* GetHandAnchorForPlayerSlot(EShowDownPlayerSlot Slot) const;
	ASDCardPlacementAnchor* GetForeheadAnchorForPlayerSlot(EShowDownPlayerSlot Slot) const;
	ASDPlayerSeat* GetSeatForSide(EShowDownSide Side) const;
	ASDPlayerSeat* GetPrimaryPlayerSeat() const;
	USceneComponent* GetHandSlotForSide(EShowDownSide Side) const;
	USceneComponent* GetHeadSlotForSide(EShowDownSide Side) const;
	USceneComponent* GetPlayerHandSlot() const;
	USceneComponent* GetPlayerHeadSlot() const;
	FSDCardHandLayoutSettings ResolveHandLayoutSettingsForPlayerState(ASDPlayerState* Player) const;
	void ApplyCardMotionForPlayerState(ASDPlayerState* Player, const TArray<ACard*>& Cards) const;
	void ScheduleRevealAutoAdvanceIfNeeded(float MinimumDelaySeconds = 0.0f);
	void ShowEventDebugMessage(const FString& Message) const;

	friend class FShowDownSingleRouletteAmmoStatusTest;
	
};
