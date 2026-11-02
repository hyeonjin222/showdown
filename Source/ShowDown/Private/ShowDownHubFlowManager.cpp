#include "ShowDownHubFlowManager.h"

#include "Audio/ShowDownAudioSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Guid.h"
#include "TimerManager.h"
#include "ShowDownGameModeBase.h"
#include "ShowDownGameStateBase.h"
#include "ShowDownCameraAspect.h"
#include "ShowDownCharacterSkinCatalog.h"
#include "ShowDownEosSubsystem.h"
#include "ShowDownLobbyWidget.h"
#include "ShowDownLoginWidget.h"
#include "ShowDownMainMenuWidget.h"
#include "ShowDownMultiplayerWidget.h"
#include "ShowDownPlayerController.h"
#include "ShowDownRankWidget.h"
#include "ShowDownShopPreviewActor.h"
#include "ShowDownShopWidget.h"
#include "ShowDownSettingsWidget.h"
#include "ShowDownTransitionWidget.h"
#include "ShowDownVoiceSubsystem.h"
#include "SupabaseSubsystem.h"
#include "UObject/ConstructorHelpers.h"

AShowDownHubFlowManager::AShowDownHubFlowManager()
{
	PrimaryActorTick.bCanEverTick = false;
	ShopWidgetClass = UShowDownShopWidget::StaticClass();
	TransitionWidgetClass = UShowDownTransitionWidget::StaticClass();
	static ConstructorHelpers::FObjectFinder<UShowDownCharacterSkinCatalog> DefaultCharacterSkinCatalog(
		TEXT("/Game/Data/Characters/DA_CharacterSkinCatalog"));
	if (DefaultCharacterSkinCatalog.Succeeded())
	{
		CharacterSkinCatalog = DefaultCharacterSkinCatalog.Object;
	}
	static ConstructorHelpers::FClassFinder<UShowDownShopWidget> ShopWidgetBlueprint(TEXT("/Game/UI/WBP_Shop"));
	if (ShopWidgetBlueprint.Succeeded()) ShopWidgetClass = ShopWidgetBlueprint.Class;
	static ConstructorHelpers::FClassFinder<UShowDownMultiplayerWidget> MultiplayerWidgetBlueprint(TEXT("/Game/UI/WBP_Multiplayer"));
	MultiplayerWidgetClass = UShowDownMultiplayerWidget::StaticClass();
	if (MultiplayerWidgetBlueprint.Succeeded()) MultiplayerWidgetClass = MultiplayerWidgetBlueprint.Class;
	static ConstructorHelpers::FClassFinder<UShowDownLobbyWidget> LobbyWidgetBlueprint(TEXT("/Game/UI/WBP_Lobby"));
	LobbyWidgetClass = UShowDownLobbyWidget::StaticClass();
	if (LobbyWidgetBlueprint.Succeeded()) LobbyWidgetClass = LobbyWidgetBlueprint.Class;
	static ConstructorHelpers::FClassFinder<UShowDownSettingsWidget> SettingsWidgetBlueprint(TEXT("/Game/UI/WBP_Settings"));
	SettingsWidgetClass = UShowDownSettingsWidget::StaticClass();
	if (SettingsWidgetBlueprint.Succeeded()) SettingsWidgetClass = SettingsWidgetBlueprint.Class;
}

bool AShowDownHubFlowManager::ShouldDisplayEosSessionStatus(
	bool bSuccess,
	bool bSessionOperationInProgress,
	const FString& Message)
{
	const FString SanitizedMessage = Message.TrimStartAndEnd();
	return !bSuccess
		&& !bSessionOperationInProgress
		&& !SanitizedMessage.IsEmpty()
		&& !SanitizedMessage.EndsWith(TEXT("..."));
}

void AShowDownHubFlowManager::BeginPlay()
{
	Super::BeginPlay();
	if (IsValid(MainMenuPreviewActor)
		&& MainMenuPreviewActor == ShopPreviewActor)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("MainMenuPreviewActor and ShopPreviewActor reference the same placed actor. Assign two separate actors so each camera shot can be composed independently."));
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USupabaseSubsystem* SupabaseSubsystem = GameInstance->GetSubsystem<USupabaseSubsystem>())
		{
			SupabaseSubsystem->OnCosmeticDataLoaded.RemoveDynamic(
				this,
				&AShowDownHubFlowManager::HandleHubCosmeticDataLoaded);
			SupabaseSubsystem->OnCosmeticDataLoaded.AddDynamic(
				this,
				&AShowDownHubFlowManager::HandleHubCosmeticDataLoaded);
			SupabaseSubsystem->OnSkinEquipped.RemoveDynamic(
				this,
				&AShowDownHubFlowManager::HandleHubSkinEquipped);
			SupabaseSubsystem->OnSkinEquipped.AddDynamic(
				this,
				&AShowDownHubFlowManager::HandleHubSkinEquipped);
		}

		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			EosSubsystem->OnManagedTravelFailed.RemoveDynamic(this, &AShowDownHubFlowManager::HandleManagedTravelFailed);
			EosSubsystem->OnManagedTravelFailed.AddDynamic(this, &AShowDownHubFlowManager::HandleManagedTravelFailed);
		}
	}
	DeactivateCharacterPreviews();
	ApplySinglePlayerVoiceSettings();

	bool bHasSession = false;
	bool bInMultiplayerLobby = false;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (const USupabaseSubsystem* SupabaseSubsystem = GameInstance->GetSubsystem<USupabaseSubsystem>())
		{
			// 오프라인 게스트도 허브로 돌아왔을 때 로그인 화면이 아니라 메인 메뉴로 복귀시킵니다.
			bHasSession = !SupabaseSubsystem->GetAccessToken().IsEmpty() || SupabaseSubsystem->IsOfflineGuest();
		}

		if (const UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			bInMultiplayerLobby = EosSubsystem->IsInMultiplayerLobby();
		}
	}

	// 시작 순간엔 폰 스폰 카메라에서 패닝되지 않도록 시작 화면 카메라로 즉시 컷합니다.
	// 이후 ShowLogin/ShowMainMenu의 블렌드는 같은 카메라로의 블렌드라 화면 이동이 보이지 않습니다.
	if (GetNetMode() != NM_Standalone && !bInMultiplayerLobby)
	{
		UE_LOG(LogTemp, Log, TEXT("HubFlowManager disabled on networked gameplay map."));
		return;
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UShowDownAudioSubsystem* AudioSubsystem = GameInstance->GetSubsystem<UShowDownAudioSubsystem>())
		{
			AudioSubsystem->SetCrowdBedEnabled(false, 0.0f);
		}
	}

#if UE_BUILD_SHIPPING
	const bool bShouldDeveloperAutoStart = false;
#else
	const bool bShouldDeveloperAutoStart =
		bDeveloperAutoStartSinglePlayer
		&& !bInMultiplayerLobby
		&& GetNetMode() == NM_Standalone;
#endif

	PlayCamera(bShouldDeveloperAutoStart ? MainMenuCamera : (bHasSession ? MainMenuCamera : LoginCamera), true);

	// 게임 종료(승/패)를 받아 허브로 복귀하기 위해 GameState 이벤트를 구독합니다.
	if (UWorld* World = GetWorld())
	{
		if (AShowDownGameStateBase* ShowDownGameState = World->GetGameState<AShowDownGameStateBase>())
		{
			BoundGameState = ShowDownGameState;
			ShowDownGameState->OnGameOver.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleGameOver);
		}
	}

	if (bShouldDeveloperAutoStart)
	{
		GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &AShowDownHubFlowManager::StartDeveloperSinglePlayPreview));
		return;
	}

	if (bInMultiplayerLobby)
	{
		ShowLobby();
	}
	else if (bHasSession)
	{
		ShowMainMenu();
	}
	else
	{
		ShowLogin();
	}
}

void AShowDownHubFlowManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearSinglePlayerCameraBlendCompletion();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReturnToHubTimerHandle);
	}

	if (AShowDownGameStateBase* ShowDownGameState = BoundGameState.Get())
	{
		ShowDownGameState->OnGameOver.RemoveDynamic(this, &AShowDownHubFlowManager::HandleGameOver);
	}
	BoundGameState.Reset();

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USupabaseSubsystem* SupabaseSubsystem = GameInstance->GetSubsystem<USupabaseSubsystem>())
		{
			SupabaseSubsystem->OnCosmeticDataLoaded.RemoveDynamic(
				this,
				&AShowDownHubFlowManager::HandleHubCosmeticDataLoaded);
			SupabaseSubsystem->OnSkinEquipped.RemoveDynamic(
				this,
				&AShowDownHubFlowManager::HandleHubSkinEquipped);
		}

		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			EosSubsystem->OnEosLoginResult.RemoveDynamic(this, &AShowDownHubFlowManager::HandleEosLoginForMultiplayer);
			EosSubsystem->OnSessionResult.RemoveDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->OnPublicRoomsUpdated.RemoveDynamic(this, &AShowDownHubFlowManager::HandlePublicRoomsUpdated);
			EosSubsystem->OnManagedTravelFailed.RemoveDynamic(this, &AShowDownHubFlowManager::HandleManagedTravelFailed);
			EosSubsystem->StopLobbyStartPolling();
		}
	}

	bPendingMultiplayerOpenAfterEosLogin = false;
	if (TransitionWidget)
	{
		TransitionWidget->RemoveFromParent();
		TransitionWidget = nullptr;
	}
	TransitionOperation = EShowDownHubTransitionOperation::None;
	SetActiveWidget(nullptr);
	DeactivateCharacterPreviews();
	LoginWidget = nullptr;
	MainMenuWidget = nullptr;
	ShopWidget = nullptr;
	RankWidget = nullptr;
	MultiplayerWidget = nullptr;
	LobbyWidget = nullptr;
	SettingsWidget = nullptr;

	Super::EndPlay(EndPlayReason);
}

void AShowDownHubFlowManager::ShowLogin()
{
	ClearSinglePlayerCameraBlendCompletion();
	if (AShowDownPlayerController* PlayerController = Cast<AShowDownPlayerController>(GetPrimaryPlayerController()))
	{
		PlayerController->DisableGameplayChat();
	}
	PlayCamera(LoginCamera);

	if (!LoginWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("LoginWidgetClass is not set on ShowDownHubFlowManager."));
		return;
	}

	LoginWidget = CreateWidget<UShowDownLoginWidget>(
		GetPrimaryPlayerController(),
		LoginWidgetClass
	);

	if (!LoginWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to create LoginWidget."));
		return;
	}

	LoginWidget->SetUseLegacyNavigation(false);
	LoginWidget->OnLoginSucceeded.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleLoginSucceeded);

	SetActiveWidget(LoginWidget);
	SetUiOnlyInput(LoginWidget);
	LoginWidget->FocusIdInput();
	OnScreenChanged.Broadcast(EShowDownHubFlowScreen::Login);
}

void AShowDownHubFlowManager::ShowMainMenu()
{
	ClearSinglePlayerCameraBlendCompletion();
	if (AShowDownPlayerController* PlayerController = Cast<AShowDownPlayerController>(GetPrimaryPlayerController()))
	{
		PlayerController->DisableGameplayChat();
	}
	PlayCamera(MainMenuCamera);

	if (!MainMenuWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("MainMenuWidgetClass is not set on ShowDownHubFlowManager."));
		return;
	}

	MainMenuWidget = CreateWidget<UShowDownMainMenuWidget>(
		GetPrimaryPlayerController(),
		MainMenuWidgetClass
	);

	if (!MainMenuWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to create MainMenuWidget."));
		return;
	}

	MainMenuWidget->SetUseLegacyNavigation(false);
	MainMenuWidget->OnSinglePlayRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleSinglePlayRequested);
	MainMenuWidget->OnMultiplayerRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleMultiplayerRequested);
	MainMenuWidget->OnShopRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleShopRequested);
	MainMenuWidget->OnRankingRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleRankingRequested);
	MainMenuWidget->OnQuitRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleQuitRequested);

	SetActiveWidget(MainMenuWidget);
	SetUiOnlyInput(MainMenuWidget);
	OnScreenChanged.Broadcast(EShowDownHubFlowScreen::MainMenu);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			FString PendingHubError;
			if (EosSubsystem->ConsumePendingHubError(PendingHubError))
			{
				MainMenuWidget->ShowStatusMessage(PendingHubError, FLinearColor::Red);
			}
		}

		if (USupabaseSubsystem* SupabaseSubsystem = GameInstance->GetSubsystem<USupabaseSubsystem>())
		{
			SupabaseSubsystem->EnsureCosmeticDataLoaded();
		}
	}
}

void AShowDownHubFlowManager::ShowShop()
{
	PlayCamera(ShopCamera ? ShopCamera : MainMenuCamera);

	TSubclassOf<UShowDownShopWidget> WidgetClass = ShopWidgetClass;
	if (!WidgetClass)
	{
		WidgetClass = UShowDownShopWidget::StaticClass();
	}

	ShopWidget = CreateWidget<UShowDownShopWidget>(
		GetPrimaryPlayerController(),
		WidgetClass
	);

	if (!ShopWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to create ShopWidget."));
		return;
	}

	ShopWidget->SetUseLegacyBackNavigation(false);
	ShopWidget->SetSkinCatalog(ResolveCharacterSkinCatalog());
	ShopWidget->OnBackRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleShopBackRequested);
	ShopWidget->OnPreviewSkinChanged.AddUniqueDynamic(
		this,
		&AShowDownHubFlowManager::HandleShopPreviewSkinChanged);

	SetActiveWidget(ShopWidget);
	SetUiOnlyInput(ShopWidget);
	ShopWidget->SetKeyboardFocus();
	OnScreenChanged.Broadcast(EShowDownHubFlowScreen::Shop);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USupabaseSubsystem* SupabaseSubsystem = GameInstance->GetSubsystem<USupabaseSubsystem>())
		{
			// Subscribe the widget before starting a request so even synchronous
			// validation failures are visible on the shop screen.
			SupabaseSubsystem->EnsureCosmeticDataLoaded();
		}
	}
}

void AShowDownHubFlowManager::ShowRanking()
{
	if (!RankWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("RankWidgetClass is not set on ShowDownHubFlowManager."));
		return;
	}

	// 랭킹 전용 카메라가 지정돼 있으면 그쪽으로, 없으면 메뉴 카메라 시점으로 블렌드합니다.
	if (!PlayCamera(RankingCamera))
	{
		PlayCamera(MainMenuCamera);
	}

	RankWidget = CreateWidget<UShowDownRankWidget>(
		GetPrimaryPlayerController(),
		RankWidgetClass
	);

	if (!RankWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to create RankWidget."));
		return;
	}

	// 랭킹 화면의 "뒤로" 버튼을 메인메뉴 복귀에 연결합니다.
	RankWidget->OnBackRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleRankBackRequested);

	SetActiveWidget(RankWidget);
	SetUiOnlyInput(RankWidget);
	OnScreenChanged.Broadcast(EShowDownHubFlowScreen::Ranking);
}

void AShowDownHubFlowManager::ShowMultiplayerMenu()
{
	UE_LOG(LogTemp, Log, TEXT("Showing multiplayer menu."));
	if (!PlayCamera(MultiplayerCamera))
	{
		PlayCamera(MainMenuCamera);
	}

	TSubclassOf<UShowDownMultiplayerWidget> WidgetClass = MultiplayerWidgetClass;
	if (!WidgetClass)
	{
		WidgetClass = UShowDownMultiplayerWidget::StaticClass();
	}

	MultiplayerWidget = CreateWidget<UShowDownMultiplayerWidget>(
		GetPrimaryPlayerController(),
		WidgetClass
	);

	if (!MultiplayerWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to create MultiplayerWidget."));
		return;
	}

	MultiplayerWidget->OnHostRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleHostMultiplayerRequested);
	MultiplayerWidget->OnPrivateHostRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleHostPrivateMultiplayerRequested);
	MultiplayerWidget->OnJoinRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleJoinMultiplayerRequested);
	MultiplayerWidget->OnRefreshRoomsRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleRefreshPublicRoomsRequested);
	MultiplayerWidget->OnJoinPublicRoomRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleJoinPublicRoomRequested);
	MultiplayerWidget->OnBackRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleMultiplayerBackRequested);

	SetActiveWidget(MultiplayerWidget);
	SetUiOnlyInput(MultiplayerWidget);
	OnScreenChanged.Broadcast(EShowDownHubFlowScreen::Multiplayer);
	UE_LOG(LogTemp, Log, TEXT("Multiplayer menu added to viewport."));
}

void AShowDownHubFlowManager::ShowLobby()
{
	UE_LOG(LogTemp, Log, TEXT("Showing multiplayer lobby."));
	if (!PlayCamera(MultiplayerCamera))
	{
		PlayCamera(MainMenuCamera);
	}

	TSubclassOf<UShowDownLobbyWidget> WidgetClass = LobbyWidgetClass;
	if (!WidgetClass)
	{
		WidgetClass = UShowDownLobbyWidget::StaticClass();
	}

	LobbyWidget = CreateWidget<UShowDownLobbyWidget>(
		GetPrimaryPlayerController(),
		WidgetClass
	);

	if (!LobbyWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to create LobbyWidget."));
		return;
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			LobbyWidget->SetLobbyInfo(EosSubsystem->GetLobbyRoomName(), EosSubsystem->GetLobbyCode(), EosSubsystem->IsLobbyHost());
			EosSubsystem->OnSessionResult.RemoveDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->OnSessionResult.AddDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->OnPublicRoomsUpdated.RemoveDynamic(this, &AShowDownHubFlowManager::HandlePublicRoomsUpdated);
			if (EosSubsystem->IsLobbyHost())
			{
				EosSubsystem->StopLobbyStartPolling();
			}
			else
			{
				EosSubsystem->StartLobbyStartPolling();
			}
		}
	}

	LobbyWidget->OnStartRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleLobbyStartRequested);
	LobbyWidget->OnLeaveRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleLobbyLeaveRequested);
	LobbyWidget->OnKickRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleLobbyKickRequested);

	SetActiveWidget(LobbyWidget);
	SetUiOnlyInput(LobbyWidget);
	OnScreenChanged.Broadcast(EShowDownHubFlowScreen::Lobby);
}

void AShowDownHubFlowManager::ShowLobbyKickResult(bool bSuccess)
{
	if (!LobbyWidget)
	{
		return;
	}

	LobbyWidget->SetInteractionPending(false);
	LobbyWidget->ShowStatusMessage(
		bSuccess
			? TEXT("참가자를 방에서 내보냈습니다.")
			: TEXT("참가자를 강퇴할 수 없습니다."),
		bSuccess ? FLinearColor::Green : FLinearColor::Red);
}

void AShowDownHubFlowManager::ShowSettings()
{
	if (!PlayCamera(OptionsCamera))
	{
		PlayCamera(MainMenuCamera);
	}
	TSubclassOf<UShowDownSettingsWidget> WidgetClass = SettingsWidgetClass;
	if (!WidgetClass) WidgetClass = UShowDownSettingsWidget::StaticClass();
	SettingsWidget = CreateWidget<UShowDownSettingsWidget>(GetPrimaryPlayerController(), WidgetClass);
	if (!SettingsWidget) return;
	SettingsWidget->OnBackRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleSettingsBackRequested);
	SettingsWidget->OnQuitRequested.AddUniqueDynamic(this, &AShowDownHubFlowManager::HandleSettingsQuitRequested);
	SetActiveWidget(SettingsWidget);
	SetUiOnlyInput(SettingsWidget);
	OnScreenChanged.Broadcast(EShowDownHubFlowScreen::Settings);
}

void AShowDownHubFlowManager::ShowSinglePlayPreview()
{
	ShowSinglePlayPreviewInternal(true);
}

void AShowDownHubFlowManager::StartDeveloperSinglePlayPreview()
{
	ShowSinglePlayPreviewInternal(!bDeveloperSkipOnlineReward);
}

void AShowDownHubFlowManager::ShowSinglePlayPreviewInternal(bool bAllowOnlineReward)
{
	ApplySinglePlayerVoiceSettings();
	CurrentRewardMatchId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	UE_LOG(LogTemp, Log, TEXT("Single play reward match id: %s"), *CurrentRewardMatchId);
	bCurrentMatchAllowsOnlineReward = false;
	if (bAllowOnlineReward)
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (const USupabaseSubsystem* SupabaseSubsystem = GameInstance->GetSubsystem<USupabaseSubsystem>())
			{
				bCurrentMatchAllowsOnlineReward = !SupabaseSubsystem->GetAccessToken().IsEmpty();
			}
		}
	}

	// 메뉴 UI를 걷어내 카드 클릭/베팅 입력이 폰으로 가도록 합니다.
	SetActiveWidget(nullptr);

	APlayerController* PlayerController = GetPrimaryPlayerController();
	if (AShowDownPlayerController* ShowDownController = Cast<AShowDownPlayerController>(PlayerController))
	{
		ShowDownController->bUseCharacterPlayerCamera = true;

		float LookSensitivity = 0.08f;
		float MinPitch = -35.0f;
		float MaxPitch = 35.0f;
		float MinYawOffset = -45.0f;
		float MaxYawOffset = 45.0f;
		bool bInvertMouseY = true;
		if (UWorld* World = GetWorld())
		{
			if (const AShowDownGameModeBase* GameMode = World->GetAuthGameMode<AShowDownGameModeBase>())
			{
				LookSensitivity = GameMode->GameplayCameraLookSensitivity;
				MinPitch = GameMode->GameplayCameraMinPitch;
				MaxPitch = GameMode->GameplayCameraMaxPitch;
				MinYawOffset = GameMode->GameplayCameraMinYawOffset;
				MaxYawOffset = GameMode->GameplayCameraMaxYawOffset;
				bInvertMouseY = GameMode->bInvertGameplayCameraMouseY;
			}
		}

		ShowDownController->ClearFixedCameraMouseLook();
		ShowDownController->SetPawnCameraMouseLook(
			LookSensitivity,
			MinPitch,
			MaxPitch,
			MinYawOffset,
			MaxYawOffset,
			bInvertMouseY);
		ShowDownController->bEnablePawnCameraMouseLook = true;
		ShowDownController->bRequireRightMouseForPawnCameraLook = false;
	}

	if (PlayerController && PlayerController->GetPawn())
	{
		ArmSinglePlayerCameraBlendCompletion(PlayerController, PlayerController->GetPawn());
		PlayerController->SetViewTargetWithBlend(
			PlayerController->GetPawn(),
			CameraBlendTime,
			VTBlend_EaseInOut,
			CameraBlendEaseExponent);
	}

	// 카드 커서 트레이스·카메라 조작·베팅 핫키가 모두 폰에 전달되도록 게임 입력 모드로 전환합니다.
	if (PlayerController)
	{
		if (AShowDownPlayerController* ShowDownController = Cast<AShowDownPlayerController>(PlayerController))
		{
			ShowDownController->bHandleShowDownGameplayInput = true;
		}

		FInputModeGameOnly InputMode;
		PlayerController->SetInputMode(InputMode);
		PlayerController->bShowMouseCursor = false;
	}

	// 실제 게임 한 판을 시작합니다.
	AShowDownGameModeBase* StartedGameMode = nullptr;
	if (UWorld* World = GetWorld())
	{
		if (AShowDownGameModeBase* GameMode = World->GetAuthGameMode<AShowDownGameModeBase>())
		{
			StartedGameMode = GameMode;
			GameMode->StartSinglePlayer();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Main level GameMode is not AShowDownGameModeBase. Single play cannot start."));
		}
	}
	if (StartedGameMode && !SinglePlayerBlendCompleteHandle.IsValid())
	{
		// No camera movement was required (or no camera manager was available).
		StartedGameMode->NotifySinglePlayerGameplayCameraReady();
	}

	OnScreenChanged.Broadcast(EShowDownHubFlowScreen::SinglePlayPreview);
	UE_LOG(LogTemp, Log, TEXT("Single play started from HubFlowManager."));
}

void AShowDownHubFlowManager::ArmSinglePlayerCameraBlendCompletion(
	APlayerController* PlayerController,
	AActor* ViewTarget)
{
	ClearSinglePlayerCameraBlendCompletion();
	if (!PlayerController
		|| !ViewTarget
		|| CameraBlendTime <= KINDA_SMALL_NUMBER
		|| PlayerController->GetViewTarget() == ViewTarget
		|| !PlayerController->PlayerCameraManager)
	{
		return;
	}

	SinglePlayerBlendPlayerController = PlayerController;
	SinglePlayerBlendCameraManager = PlayerController->PlayerCameraManager;
	SinglePlayerBlendViewTarget = ViewTarget;
	SinglePlayerBlendCompleteHandle = PlayerController->PlayerCameraManager->OnBlendComplete().AddUObject(
		this,
		&AShowDownHubFlowManager::HandleSinglePlayerCameraBlendComplete);

	// Native blend completion is authoritative. The fallback only prevents a
	// replaced/cancelled view-target blend from permanently blocking gameplay.
	GetWorldTimerManager().SetTimer(
		SinglePlayerBlendFallbackTimerHandle,
		this,
		&AShowDownHubFlowManager::HandleSinglePlayerCameraBlendFallback,
		CameraBlendTime + 0.25f,
		false);
}

void AShowDownHubFlowManager::HandleSinglePlayerCameraBlendComplete()
{
	APlayerController* PlayerController = SinglePlayerBlendPlayerController.Get();
	AActor* ExpectedViewTarget = SinglePlayerBlendViewTarget.Get();
	if (!PlayerController
		|| !ExpectedViewTarget
		|| PlayerController->GetViewTarget() != ExpectedViewTarget)
	{
		return;
	}

	ClearSinglePlayerCameraBlendCompletion();
	if (UWorld* World = GetWorld())
	{
		if (AShowDownGameModeBase* GameMode = World->GetAuthGameMode<AShowDownGameModeBase>())
		{
			GameMode->NotifySinglePlayerGameplayCameraReady();
		}
	}
}

void AShowDownHubFlowManager::HandleSinglePlayerCameraBlendFallback()
{
	APlayerController* PlayerController = SinglePlayerBlendPlayerController.Get();
	AActor* ExpectedViewTarget = SinglePlayerBlendViewTarget.Get();
	if (PlayerController
		&& ExpectedViewTarget
		&& PlayerController->GetViewTarget() == ExpectedViewTarget)
	{
		HandleSinglePlayerCameraBlendComplete();
		return;
	}

	const bool bShouldRetry = bRetrySinglePlayerGameplayCameraUntilReady;
	ClearSinglePlayerCameraBlendCompletion();
	if (bShouldRetry)
	{
		// The final gameplay return may be replaced by a Blueprint camera cut.
		// Retry on the next tick until the exact pawn view is actually reached.
		SinglePlayerBlendFallbackTimerHandle = GetWorldTimerManager().SetTimerForNextTick(
			this,
			&AShowDownHubFlowManager::EnsureSinglePlayerGameplayCameraReady);
	}
}

void AShowDownHubFlowManager::ClearSinglePlayerCameraBlendCompletion()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SinglePlayerBlendFallbackTimerHandle);
	}
	if (APlayerCameraManager* CameraManager = SinglePlayerBlendCameraManager.Get();
		CameraManager && SinglePlayerBlendCompleteHandle.IsValid())
	{
		CameraManager->OnBlendComplete().Remove(SinglePlayerBlendCompleteHandle);
	}

	SinglePlayerBlendPlayerController.Reset();
	SinglePlayerBlendCameraManager.Reset();
	SinglePlayerBlendViewTarget.Reset();
	SinglePlayerBlendCompleteHandle.Reset();
	bRetrySinglePlayerGameplayCameraUntilReady = false;
}

void AShowDownHubFlowManager::EnsureSinglePlayerGameplayCameraReady()
{
	APlayerController* PlayerController = GetPrimaryPlayerController();
	AActor* GameplayViewTarget = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!PlayerController || !GameplayViewTarget)
	{
		return;
	}

	if (PlayerController->GetViewTarget() == GameplayViewTarget)
	{
		ClearSinglePlayerCameraBlendCompletion();
		if (UWorld* World = GetWorld())
		{
			if (AShowDownGameModeBase* GameMode = World->GetAuthGameMode<AShowDownGameModeBase>())
			{
				GameMode->NotifySinglePlayerGameplayCameraReady();
			}
		}
		return;
	}

	ArmSinglePlayerCameraBlendCompletion(PlayerController, GameplayViewTarget);
	bRetrySinglePlayerGameplayCameraUntilReady = true;
	PlayerController->SetViewTargetWithBlend(
		GameplayViewTarget,
		CameraBlendTime,
		EViewTargetBlendFunction::VTBlend_EaseInOut,
		CameraBlendEaseExponent,
		false);

	if (!SinglePlayerBlendCompleteHandle.IsValid()
		&& PlayerController->GetViewTarget() == GameplayViewTarget)
	{
		// Zero-duration blends are synchronous and never broadcast OnBlendComplete.
		bRetrySinglePlayerGameplayCameraUntilReady = false;
		if (UWorld* World = GetWorld())
		{
			if (AShowDownGameModeBase* GameMode = World->GetAuthGameMode<AShowDownGameModeBase>())
			{
				GameMode->NotifySinglePlayerGameplayCameraReady();
			}
		}
	}
}

void AShowDownHubFlowManager::ApplySinglePlayerVoiceSettings()
{
	if (UShowDownVoiceSubsystem* VoiceSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UShowDownVoiceSubsystem>()
		: nullptr)
	{
		VoiceSubsystem->TTSPlaybackSpeed = FMath::Clamp(SinglePlayerVoiceSpeed, 0.5f, 2.0f);
		VoiceSubsystem->TTSPlaybackPitch = FMath::Clamp(SinglePlayerVoicePitch, 0.5f, 2.0f);
	}
}

void AShowDownHubFlowManager::PrepareForMultiplayerGameplay()
{
	// Remove the lobby/menu layer explicitly. RemoveAllViewportWidgets would also
	// clear screen-space WidgetComponent layers (character name tags) while those
	// components still believe they are registered with the viewport.
	if (TransitionWidget)
	{
		TransitionWidget->RemoveFromParent();
		TransitionWidget = nullptr;
	}
	TransitionOperation = EShowDownHubTransitionOperation::None;
	SetActiveWidget(nullptr);
	MultiplayerWidget = nullptr;
	LobbyWidget = nullptr;
}

void AShowDownHubFlowManager::OpenMultiplayerLevel()
{
	if (MultiplayerLevelName.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("MultiplayerLevelName is not set."));
		return;
	}

	UGameplayStatics::OpenLevel(this, MultiplayerLevelName);
}

void AShowDownHubFlowManager::QuitGame()
{
	if (APlayerController* PlayerController = GetPrimaryPlayerController())
	{
		UKismetSystemLibrary::QuitGame(
			this,
			PlayerController,
			EQuitPreference::Quit,
			false
		);
	}
}

void AShowDownHubFlowManager::SetActiveWidget(UUserWidget* NextWidget)
{
	if (ActiveWidget)
	{
		ActiveWidget->RemoveFromParent();
	}

	ActiveWidget = NextWidget;
	UpdateCharacterPreviewsForWidget(NextWidget);

	if (ActiveWidget)
	{
		ActiveWidget->AddToViewport();
		BindTopNavigation(ActiveWidget);
	}
}

void AShowDownHubFlowManager::ShowTransitionOverlay(
	EShowDownHubTransitionOperation Operation,
	const FString& Title,
	const FString& Detail)
{
	TransitionOperation = Operation;
	if (MultiplayerWidget)
	{
		MultiplayerWidget->SetInteractionPending(true);
	}
	if (LobbyWidget)
	{
		LobbyWidget->SetInteractionPending(true);
	}

	if (!TransitionWidget)
	{
		TSubclassOf<UShowDownTransitionWidget> WidgetClass = TransitionWidgetClass;
		if (!WidgetClass)
		{
			WidgetClass = UShowDownTransitionWidget::StaticClass();
		}
		TransitionWidget = CreateWidget<UShowDownTransitionWidget>(GetPrimaryPlayerController(), WidgetClass);
	}

	if (!TransitionWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to create multiplayer transition overlay."));
		TransitionOperation = EShowDownHubTransitionOperation::None;
		if (MultiplayerWidget)
		{
			MultiplayerWidget->SetInteractionPending(false);
		}
		if (LobbyWidget)
		{
			LobbyWidget->SetInteractionPending(false);
		}
		return;
	}

	TransitionWidget->SetTransitionText(Title, Detail);
	if (!TransitionWidget->IsInViewport())
	{
		TransitionWidget->AddToViewport(10000);
	}
}

void AShowDownHubFlowManager::HideTransitionOverlay()
{
	if (TransitionWidget)
	{
		TransitionWidget->Dismiss();
		TransitionWidget = nullptr;
	}

	TransitionOperation = EShowDownHubTransitionOperation::None;
	if (MultiplayerWidget)
	{
		MultiplayerWidget->SetInteractionPending(false);
	}
	if (LobbyWidget)
	{
		LobbyWidget->SetInteractionPending(false);
	}
}

void AShowDownHubFlowManager::BindTopNavigation(UUserWidget* Widget)
{
	// Lobby navigation is intentionally handled by its leave flow so the EOS
	// session is closed cleanly before another hub screen is opened.
	if (!Widget || Cast<UShowDownLobbyWidget>(Widget)) return;
	auto Bind = [Widget](const TCHAR* Name, UObject* Object, FName FunctionName)
	{
		if (UButton* Button = Cast<UButton>(Widget->GetWidgetFromName(Name)))
		{
			FScriptDelegate Delegate;
			Delegate.BindUFunction(Object, FunctionName);
			Button->OnClicked.AddUnique(Delegate);
		}
	};
	Bind(TEXT("Nav_0"), this, GET_FUNCTION_NAME_CHECKED(AShowDownHubFlowManager, HandleTopNavSinglePlay));
	Bind(TEXT("Nav_1"), this, GET_FUNCTION_NAME_CHECKED(AShowDownHubFlowManager, HandleTopNavMultiplayer));
	Bind(TEXT("Nav_2"), this, GET_FUNCTION_NAME_CHECKED(AShowDownHubFlowManager, HandleTopNavShop));
	Bind(TEXT("Nav_3"), this, GET_FUNCTION_NAME_CHECKED(AShowDownHubFlowManager, HandleTopNavRanking));
	Bind(TEXT("Nav_4"), this, GET_FUNCTION_NAME_CHECKED(AShowDownHubFlowManager, HandleTopNavSettings));
}

void AShowDownHubFlowManager::HandleTopNavSinglePlay() { HandleSinglePlayRequested(); }
void AShowDownHubFlowManager::HandleTopNavMultiplayer() { ShowMultiplayerMenu(); }
void AShowDownHubFlowManager::HandleTopNavShop() { ShowShop(); }
void AShowDownHubFlowManager::HandleTopNavRanking() { ShowRanking(); }
void AShowDownHubFlowManager::HandleTopNavSettings() { ShowSettings(); }

void AShowDownHubFlowManager::SetUiOnlyInput(UUserWidget* FocusWidget)
{
	ClearGameplayCameraLook();

	if (APlayerController* PlayerController = GetPrimaryPlayerController())
	{
		if (AShowDownPlayerController* ShowDownController = Cast<AShowDownPlayerController>(PlayerController))
		{
			ShowDownController->bHandleShowDownGameplayInput = false;
		}

		PlayerController->bShowMouseCursor = true;

		FInputModeUIOnly InputMode;

		if (FocusWidget)
		{
			FocusWidget->SetIsFocusable(true);
			InputMode.SetWidgetToFocus(FocusWidget->TakeWidget());
		}

		PlayerController->SetInputMode(InputMode);
	}
}

bool AShowDownHubFlowManager::PlayCamera(ACameraActor* Camera, bool bCut)
{
	ShowDownCameraAspect::ApplyForced16By9(Camera);
	return PlayViewTarget(Camera, bCut);
}

bool AShowDownHubFlowManager::PlayViewTarget(AActor* ViewTarget, bool bCut)
{
	if (!ViewTarget)
	{
		return false;
	}

	if (APlayerController* PlayerController = GetPrimaryPlayerController())
	{
		if (ACameraActor* CameraActor = Cast<ACameraActor>(ViewTarget))
		{
			ShowDownCameraAspect::ApplyForced16By9(CameraActor);
		}

		if (bCut || CameraBlendTime <= 0.0f)
		{
			PlayerController->SetViewTarget(ViewTarget);
		}
		else
		{
			PlayerController->SetViewTargetWithBlend(
				ViewTarget,
				CameraBlendTime,
				VTBlend_EaseInOut,
				CameraBlendEaseExponent);
		}
		return true;
	}

	return false;
}

void AShowDownHubFlowManager::ClearGameplayCameraLook()
{
	if (AShowDownPlayerController* ShowDownController = Cast<AShowDownPlayerController>(GetPrimaryPlayerController()))
	{
		ShowDownController->ClearFixedCameraMouseLook();
	}
}

APlayerController* AShowDownHubFlowManager::GetPrimaryPlayerController() const
{
	return UGameplayStatics::GetPlayerController(this, 0);
}

void AShowDownHubFlowManager::UpdateCharacterPreviewsForWidget(UUserWidget* NextWidget)
{
	if (NextWidget && NextWidget == LoginWidget)
	{
		DeactivateCharacterPreviews();
		return;
	}

	if (NextWidget && NextWidget == ShopWidget)
	{
		if (IsValid(MainMenuPreviewActor))
		{
			MainMenuPreviewActor->DeactivatePreview();
		}
		if (!IsValid(ShopPreviewActor))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("ShopPreviewActor is not assigned on the placed HubFlowManager."));
			return;
		}

		ShopPreviewActor->SetSkinCatalog(ResolveCharacterSkinCatalog());
		ShopPreviewActor->SetPreviewContext(EShowDownCharacterPreviewContext::Shop);
		ShopPreviewActor->ActivatePreview(
			UShowDownCharacterSkinCatalog::GetDefaultSkinId());
		return;
	}

	// The equipped main-menu character is part of the hub/game scene, not just
	// the main-menu widget. Keep it alive through single-player startup and the
	// multiplayer browser/lobby flow. A nullptr widget means gameplay or travel
	// is taking over, so it must not implicitly hide the placed character either.
	if (IsValid(ShopPreviewActor))
	{
		ShopPreviewActor->DeactivatePreview();
	}
	if (!IsValid(MainMenuPreviewActor) || !MainMenuPreviewActor->IsPreviewActive())
	{
		RefreshMainMenuCharacterPreview();
	}
}

void AShowDownHubFlowManager::DeactivateCharacterPreviews()
{
	if (IsValid(ShopPreviewActor))
	{
		ShopPreviewActor->DeactivatePreview();
	}
	if (IsValid(MainMenuPreviewActor))
	{
		MainMenuPreviewActor->DeactivatePreview();
	}
}

void AShowDownHubFlowManager::HandleShopPreviewSkinChanged(const FString& SkinId)
{
	if (ActiveWidget == ShopWidget && IsValid(ShopPreviewActor))
	{
		ShopPreviewActor->SetPreviewSkin(SkinId);
	}
}

void AShowDownHubFlowManager::RefreshMainMenuCharacterPreview()
{
	if (!IsValid(MainMenuPreviewActor))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MainMenuPreviewActor is not assigned on the placed HubFlowManager."));
		return;
	}

	FString EquippedSkinId = UShowDownCharacterSkinCatalog::GetDefaultSkinId();
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const USupabaseSubsystem* SupabaseSubsystem =
			GameInstance->GetSubsystem<USupabaseSubsystem>())
		{
			const FString LoadedSkinId = SupabaseSubsystem->GetEquippedSkinId(TEXT("character"));
			if (!LoadedSkinId.IsEmpty())
			{
				EquippedSkinId = LoadedSkinId;
			}
		}
	}

	MainMenuPreviewActor->SetSkinCatalog(ResolveCharacterSkinCatalog());
	MainMenuPreviewActor->SetPreviewContext(EShowDownCharacterPreviewContext::MainMenu);
	MainMenuPreviewActor->ActivatePreview(EquippedSkinId);
}

UShowDownCharacterSkinCatalog* AShowDownHubFlowManager::ResolveCharacterSkinCatalog() const
{
	// Hub UI, manually placed preview actors, network validation, and gameplay characters
	// must all resolve custom skin IDs against the same catalog asset.
	if (UShowDownCharacterSkinCatalog* DefaultCatalog =
		UShowDownCharacterSkinCatalog::LoadDefaultCatalog())
	{
		return DefaultCatalog;
	}

	if (CharacterSkinCatalog)
	{
		return CharacterSkinCatalog;
	}
	if (IsValid(ShopPreviewActor) && ShopPreviewActor->GetSkinCatalog())
	{
		return ShopPreviewActor->GetSkinCatalog();
	}
	if (IsValid(MainMenuPreviewActor) && MainMenuPreviewActor->GetSkinCatalog())
	{
		return MainMenuPreviewActor->GetSkinCatalog();
	}
	return nullptr;
}

void AShowDownHubFlowManager::HandleHubCosmeticDataLoaded(
	const bool bSuccess,
	const FString& Message)
{
	if (bSuccess && ActiveWidget == MainMenuWidget)
	{
		RefreshMainMenuCharacterPreview();
	}
}

void AShowDownHubFlowManager::HandleHubSkinEquipped(
	const bool bSuccess,
	const FString& Message)
{
	if (bSuccess && ActiveWidget == MainMenuWidget)
	{
		RefreshMainMenuCharacterPreview();
	}
}

void AShowDownHubFlowManager::HandleLoginSucceeded()
{
	ShowMainMenu();
}

void AShowDownHubFlowManager::HandleSinglePlayRequested()
{
	ShowSinglePlayPreview();
}

void AShowDownHubFlowManager::HandleMultiplayerRequested()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			if (EosSubsystem->IsEosLoggedIn())
			{
				ShowMultiplayerMenu();
				return;
			}

			bPendingMultiplayerOpenAfterEosLogin = true;
			EosSubsystem->OnEosLoginResult.RemoveDynamic(this, &AShowDownHubFlowManager::HandleEosLoginForMultiplayer);
			EosSubsystem->OnEosLoginResult.AddDynamic(this, &AShowDownHubFlowManager::HandleEosLoginForMultiplayer);
			EosSubsystem->LoginWithSupabaseSession();
			return;
		}
	}

	if (MainMenuWidget)
	{
		MainMenuWidget->ShowStatusMessage(TEXT("EOS subsystem is unavailable."), FLinearColor::Red);
	}
	UE_LOG(LogTemp, Warning, TEXT("EOS subsystem is unavailable. Multiplayer level was not opened."));
}

void AShowDownHubFlowManager::HandleEosLoginForMultiplayer(bool bSuccess, const FString& Message)
{
	UE_LOG(LogTemp, Log, TEXT("EOS login for multiplayer: %s"), *Message);

	if (Message == TEXT("Logging in to EOS..."))
	{
		return;
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			EosSubsystem->OnEosLoginResult.RemoveDynamic(this, &AShowDownHubFlowManager::HandleEosLoginForMultiplayer);
		}
	}

	if (!bPendingMultiplayerOpenAfterEosLogin)
	{
		return;
	}

	bPendingMultiplayerOpenAfterEosLogin = false;

	if (bSuccess)
	{
		ShowMultiplayerMenu();
		return;
	}

	if (MainMenuWidget)
	{
		MainMenuWidget->ShowStatusMessage(Message, FLinearColor::Red);
	}
}

void AShowDownHubFlowManager::HandleHostMultiplayerRequested(const FString& RoomName)
{
	ShowTransitionOverlay(
		EShowDownHubTransitionOperation::CreateRoom,
		TEXT("방을 만드는 중"),
		TEXT("네트워크 로비를 안전하게 준비하고 있습니다."));

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			EosSubsystem->OnSessionResult.RemoveDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->OnSessionResult.AddDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->HostLobby(MultiplayerLobbyLevelName, MultiplayerLevelName, RoomName);
			return;
		}
	}

	if (MultiplayerWidget)
	{
		MultiplayerWidget->ShowStatusMessage(TEXT("EOS subsystem is unavailable."), FLinearColor::Red);
	}
	HideTransitionOverlay();
}

void AShowDownHubFlowManager::HandleHostPrivateMultiplayerRequested(const FString& RoomName)
{
	ShowTransitionOverlay(
		EShowDownHubTransitionOperation::CreateRoom,
		TEXT("비공개방을 만드는 중"),
		TEXT("초대용 네트워크 로비를 준비하고 있습니다."));

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			EosSubsystem->OnSessionResult.RemoveDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->OnSessionResult.AddDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->HostPrivateLobby(MultiplayerLobbyLevelName, MultiplayerLevelName, RoomName);
			return;
		}
	}

	if (MultiplayerWidget)
	{
		MultiplayerWidget->ShowStatusMessage(TEXT("EOS subsystem is unavailable."), FLinearColor::Red);
	}
	HideTransitionOverlay();
}

void AShowDownHubFlowManager::HandleJoinMultiplayerRequested(const FString& RoomCode)
{
	ShowTransitionOverlay(
		EShowDownHubTransitionOperation::JoinRoom,
		TEXT("방에 연결하는 중"),
		TEXT("호스트를 찾고 참가 준비를 진행하고 있습니다."));

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			EosSubsystem->OnSessionResult.RemoveDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->OnSessionResult.AddDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->JoinLobbyByCode(RoomCode);
			return;
		}
	}

	if (MultiplayerWidget)
	{
		MultiplayerWidget->ShowStatusMessage(TEXT("EOS subsystem is unavailable."), FLinearColor::Red);
	}
	HideTransitionOverlay();
}

void AShowDownHubFlowManager::HandleRefreshPublicRoomsRequested()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			EosSubsystem->OnSessionResult.RemoveDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->OnSessionResult.AddDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->OnPublicRoomsUpdated.RemoveDynamic(this, &AShowDownHubFlowManager::HandlePublicRoomsUpdated);
			EosSubsystem->OnPublicRoomsUpdated.AddDynamic(this, &AShowDownHubFlowManager::HandlePublicRoomsUpdated);
			EosSubsystem->FindPublicLobbies();
			return;
		}
	}

	if (MultiplayerWidget)
	{
		MultiplayerWidget->ShowStatusMessage(TEXT("EOS subsystem is unavailable."), FLinearColor::Red);
		MultiplayerWidget->SetPublicRooms(TArray<FShowDownPublicRoomInfo>());
	}
}

void AShowDownHubFlowManager::HandleJoinPublicRoomRequested(int32 SearchResultIndex)
{
	ShowTransitionOverlay(
		EShowDownHubTransitionOperation::JoinRoom,
		TEXT("방에 입장하는 중"),
		TEXT("호스트와 연결하고 플레이어 정보를 동기화하고 있습니다."));

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			EosSubsystem->OnSessionResult.RemoveDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->OnSessionResult.AddDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->JoinPublicLobbyByIndex(SearchResultIndex);
			return;
		}
	}

	if (MultiplayerWidget)
	{
		MultiplayerWidget->ShowStatusMessage(TEXT("EOS subsystem is unavailable."), FLinearColor::Red);
	}
	HideTransitionOverlay();
}

void AShowDownHubFlowManager::HandlePublicRoomsUpdated(bool bSuccess, const TArray<FShowDownPublicRoomInfo>& Rooms)
{
	if (MultiplayerWidget)
	{
		MultiplayerWidget->SetPublicRooms(Rooms);
		if (!bSuccess)
		{
			MultiplayerWidget->ShowStatusMessage(TEXT("공개방 목록을 불러오지 못했습니다."), FLinearColor::Red);
		}
	}
}

void AShowDownHubFlowManager::HandleMultiplayerBackRequested()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			EosSubsystem->OnSessionResult.RemoveDynamic(this, &AShowDownHubFlowManager::HandleEosSessionResult);
			EosSubsystem->OnPublicRoomsUpdated.RemoveDynamic(this, &AShowDownHubFlowManager::HandlePublicRoomsUpdated);
		}
	}

	ShowMainMenu();
}

void AShowDownHubFlowManager::HandleEosSessionResult(bool bSuccess, const FString& Message)
{
	UE_LOG(LogTemp, Log, TEXT("EOS session result: %s"), *Message);

	bool bSessionOperationInProgress = false;
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			bSessionOperationInProgress = EosSubsystem->IsInteractiveSessionOperationInProgress();
		}
	}

	if (bSuccess && Message == TEXT("EOS game joined."))
	{
		SetActiveWidget(nullptr);
		ShowTransitionOverlay(
			EShowDownHubTransitionOperation::StartGame,
			TEXT("게임을 준비하는 중"),
			TEXT("게임 화면을 불러오고 있습니다."));
		LobbyWidget = nullptr;
		MultiplayerWidget = nullptr;

		if (APlayerController* PlayerController = GetPrimaryPlayerController())
		{
			FInputModeGameOnly InputMode;
			PlayerController->SetInputMode(InputMode);
			PlayerController->bShowMouseCursor = false;
			PlayerController->bEnableClickEvents = false;
			PlayerController->bEnableMouseOverEvents = false;

			if (AShowDownPlayerController* ShowDownController = Cast<AShowDownPlayerController>(PlayerController))
			{
				// Keep gameplay input locked until ClientEnterMultiplayerGameplay and
				// the authoritative seat camera are both ready.
				ShowDownController->bHandleShowDownGameplayInput = false;
			}
		}
		return;
	}

	const bool bDisplayStatus = ShouldDisplayEosSessionStatus(
		bSuccess,
		bSessionOperationInProgress,
		Message);
	if (bDisplayStatus && MultiplayerWidget)
	{
		MultiplayerWidget->ShowStatusMessage(Message, FLinearColor::Red);
	}

	if (bDisplayStatus && LobbyWidget)
	{
		LobbyWidget->ShowStatusMessage(Message, FLinearColor::Red);
	}

	if (!bSuccess
		&& !bSessionOperationInProgress
		&& TransitionOperation != EShowDownHubTransitionOperation::None
		&& TransitionOperation != EShowDownHubTransitionOperation::LeaveRoom)
	{
		HideTransitionOverlay();
	}
}

void AShowDownHubFlowManager::HandleManagedTravelFailed(const FString& Message, bool bReopenMultiplayerMenu)
{
	HideTransitionOverlay();
	if (bReopenMultiplayerMenu)
	{
		ShowMultiplayerMenu();
	}

	if (MultiplayerWidget)
	{
		MultiplayerWidget->ShowStatusMessage(Message, FLinearColor::Red);
	}
	else if (LobbyWidget)
	{
		LobbyWidget->ShowStatusMessage(Message, FLinearColor::Red);
	}
}

void AShowDownHubFlowManager::HandleLobbyStartRequested()
{
	ShowTransitionOverlay(
		EShowDownHubTransitionOperation::StartGame,
		TEXT("게임을 시작하는 중"),
		TEXT("모든 참가자의 연결을 확인하고 있습니다."));

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			EosSubsystem->StartHostedGame();
			return;
		}
	}

	HideTransitionOverlay();
}

void AShowDownHubFlowManager::HandleLobbyLeaveRequested()
{
	ShowTransitionOverlay(
		EShowDownHubTransitionOperation::LeaveRoom,
		TEXT("로비에서 나가는 중"),
		TEXT("세션을 정리하고 메인 화면으로 돌아갑니다."));

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UShowDownEosSubsystem* EosSubsystem = GameInstance->GetSubsystem<UShowDownEosSubsystem>())
		{
			EosSubsystem->LeaveLobby(FName(TEXT("L_ShowdownMain")));
			return;
		}
	}

	HideTransitionOverlay();
	ShowMainMenu();
}

void AShowDownHubFlowManager::HandleLobbyKickRequested(const FString& PlayerId)
{
	if (PlayerId.IsEmpty())
	{
		if (LobbyWidget)
		{
			LobbyWidget->SetInteractionPending(false);
			LobbyWidget->ShowStatusMessage(TEXT("강퇴할 참가자를 확인할 수 없습니다."), FLinearColor::Red);
		}
		return;
	}

	if (AShowDownPlayerController* PlayerController = Cast<AShowDownPlayerController>(GetPrimaryPlayerController()))
	{
		PlayerController->ServerRequestLobbyKick(PlayerId);
		return;
	}

	if (LobbyWidget)
	{
		LobbyWidget->SetInteractionPending(false);
		LobbyWidget->ShowStatusMessage(TEXT("강퇴 요청을 보낼 수 없습니다."), FLinearColor::Red);
	}
}

void AShowDownHubFlowManager::HandleShopRequested()
{
	ShowShop();
}

void AShowDownHubFlowManager::HandleRankingRequested()
{
	ShowRanking();
}

void AShowDownHubFlowManager::HandleQuitRequested()
{
	ShowSettings();
}

void AShowDownHubFlowManager::HandleSettingsBackRequested()
{
	ShowMainMenu();
}

void AShowDownHubFlowManager::HandleSettingsQuitRequested()
{
	QuitGame();
}

void AShowDownHubFlowManager::HandleShopBackRequested()
{
	ShowMainMenu();
}

void AShowDownHubFlowManager::HandleRankBackRequested()
{
	ShowMainMenu();
}

void AShowDownHubFlowManager::HandleGameOver(EShowDownSide Winner)
{
	if (UWorld* World = GetWorld(); World && World->GetNetMode() != NM_Standalone)
	{
		UE_LOG(LogTemp, Log, TEXT("Ignoring hub auto-return for multiplayer game over."));
		World->GetTimerManager().ClearTimer(ReturnToHubTimerHandle);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Game over. Winner: %s. Returning to hub in %.1fs."),
		Winner == EShowDownSide::Player ? TEXT("Player") : TEXT("Collector"),
		ReturnToHubDelay);

	// 플레이어가 모든 스테이지를 클리어해 승리하면 랭크 점수 보상을 지급합니다.
	// 보상 금액(10~50)은 서버의 award_win_reward RPC가 결정하므로 여기서는 호출만 합니다.
	if (Winner == EShowDownSide::Player && bCurrentMatchAllowsOnlineReward)
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (USupabaseSubsystem* SupabaseSubsystem = GameInstance->GetSubsystem<USupabaseSubsystem>())
			{
				SupabaseSubsystem->AwardWinReward(CurrentRewardMatchId);
			}
		}
	}

	// 연출 파트가 결과 카메라/위젯 연출을 붙일 수 있는 훅입니다(블루프린트에서 구현).
	OnGameResultPresentation(Winner);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 대기 시간이 0 이하면 즉시 복귀합니다.
	// (연출 파트가 길이를 직접 제어하려면 ReturnToHubDelay를 충분히 크게 두고
	//  연출이 끝날 때 FinishResultAndReturnToHub()를 호출하면 됩니다.)
	if (ReturnToHubDelay <= 0.0f)
	{
		ReturnToHub();
		return;
	}

	World->GetTimerManager().SetTimer(
		ReturnToHubTimerHandle,
		this,
		&AShowDownHubFlowManager::ReturnToHub,
		ReturnToHubDelay,
		false);
}

void AShowDownHubFlowManager::FinishResultAndReturnToHub()
{
	// 자동 복귀 타이머가 걸려 있으면 취소하고 즉시 복귀합니다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReturnToHubTimerHandle);
	}

	ReturnToHub();
}

void AShowDownHubFlowManager::ReturnToHub()
{
	// 테이블의 카드와 진행 상태를 정리합니다.
	if (UWorld* World = GetWorld())
	{
		if (AShowDownGameModeBase* GameMode = World->GetAuthGameMode<AShowDownGameModeBase>())
		{
			GameMode->ResetForHubReturn();
		}
	}

	// 보상(점수/코인) 반영분을 서버에서 다시 불러와 캐시를 최신으로 맞춥니다.
	// 새로 뜨는 메인메뉴가 OnPlayerDataLoaded를 받아 갱신된 값을 표시합니다.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USupabaseSubsystem* SupabaseSubsystem = GameInstance->GetSubsystem<USupabaseSubsystem>())
		{
			SupabaseSubsystem->LoadPlayerData();
		}
	}

	// 카메라/입력/위젯을 메인메뉴 상태로 되돌립니다.
	CurrentRewardMatchId.Empty();
	bCurrentMatchAllowsOnlineReward = false;
	ShowMainMenu();
}
