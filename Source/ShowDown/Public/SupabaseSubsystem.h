#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HttpFwd.h"
#include "SupabaseSubsystem.generated.h"

class FJsonObject;

// 로그인 요청이 끝났을 때 UI나 다른 코드에 결과를 알려주는 이벤트입니다.
// bSuccess는 성공 여부, Message는 화면에 표시할 간단한 상태 문구입니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnSupabaseLoginResult,
	bool,
	bSuccess,
	const FString&,
	Message
);

// 플레이어 데이터 로딩이 끝났을 때 결과를 알려주는 이벤트입니다.
// 지금은 profile, wallet, rank 요청이 각각 끝날 때마다 호출됩니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnPlayerDataLoaded,
	bool,
	bSuccess,
	const FString&,
	Message
);

// 닉네임 변경 요청이 끝났을 때 UI에 결과를 알려주는 이벤트입니다.
// bSuccess는 성공 여부, Message는 화면이나 로그에 보여줄 상태 문구입니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnNicknameUpdated,
	bool,
	bSuccess,
	const FString&,
	Message
);

USTRUCT(BlueprintType)
struct FShowDownSkin
{
	GENERATED_BODY()

	// ShopSkins 배열에서는 skin_sets.id를 담고,
	// SkinSetItemsBySetId 안에서는 실제 skins.id를 담습니다.
	UPROPERTY(BlueprintReadOnly, Category = "Supabase")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "Supabase")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Supabase")
	FString Rarity;

	UPROPERTY(BlueprintReadOnly, Category = "Supabase")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "Supabase")
	int32 Price = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Supabase")
	bool bIsActive = false;
};

// 리더보드 한 줄(한 유저)의 정보입니다. get_leaderboard RPC 응답을 담습니다.
// user_id/email 같은 민감 정보는 포함하지 않고, 공개해도 되는 닉네임/점수만 담습니다.
USTRUCT(BlueprintType)
struct FShowDownRankEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Supabase")
	int32 Rank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Supabase")
	FString Nickname;

	UPROPERTY(BlueprintReadOnly, Category = "Supabase")
	int32 Score = 0;
};

// 리더보드 로딩 결과를 알려주는 이벤트입니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnLeaderboardLoaded,
	bool,
	bSuccess,
	const FString&,
	Message
);

// 상점/스킨 데이터 스냅샷 로딩 결과를 알려주는 이벤트입니다.
// 관련 REST 요청이 모두 끝난 뒤 한 번만 호출됩니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnCosmeticDataLoaded,
	bool,
	bSuccess,
	const FString&,
	Message
);

// 장착 스킨 변경 요청 결과를 알려주는 이벤트입니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnSkinEquipped,
	bool,
	bSuccess,
	const FString&,
	Message
);

// 상점 상품 구매 요청 결과를 알려주는 이벤트입니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnSkinSetPurchased,
	bool,
	bSuccess,
	const FString&,
	Message
);

// Supabase와 통신하는 전용 Subsystem입니다.
// GameInstanceSubsystem이라 게임 실행 중 계속 유지되고, UI나 게임 로직에서 쉽게 꺼내 쓸 수 있습니다.
UCLASS()
class SHOWDOWN_API USupabaseSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 로그인 성공/실패를 외부에 알리는 이벤트입니다.
	// LoginWidget 같은 UI가 이 이벤트를 구독해서 상태 메시지를 바꿀 수 있습니다.
	UPROPERTY(BlueprintAssignable)
	FOnSupabaseLoginResult OnLoginResult;

	// 닉네임, 재화, 랭크 점수 로딩 결과를 외부에 알리는 이벤트입니다.
	// 지금은 디버그와 UI 표시 준비 용도로 사용합니다.
	UPROPERTY(BlueprintAssignable)
	FOnPlayerDataLoaded OnPlayerDataLoaded;

	// 상점 스킨, 보유 스킨, 장착 스킨 로딩 결과를 외부에 알리는 이벤트입니다.
	UPROPERTY(BlueprintAssignable)
	FOnCosmeticDataLoaded OnCosmeticDataLoaded;

	// 스킨 장착 변경 결과를 외부에 알리는 이벤트입니다.
	UPROPERTY(BlueprintAssignable)
	FOnSkinEquipped OnSkinEquipped;

	// 상점 상품 구매 결과를 외부에 알리는 이벤트입니다.
	UPROPERTY(BlueprintAssignable)
	FOnSkinSetPurchased OnSkinSetPurchased;

	// 리더보드 로딩 결과를 외부에 알리는 이벤트입니다.
	UPROPERTY(BlueprintAssignable)
	FOnLeaderboardLoaded OnLeaderboardLoaded;

	// 이메일과 비밀번호로 Supabase Auth에 로그인 요청을 보냅니다.
	// 성공하면 AccessToken과 UserId를 저장하고 LoadPlayerData를 호출합니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	void LoginWithEmail(const FString& Email, const FString& Password);

	// 아이디(username)와 비밀번호로 로그인합니다.
	// 아이디->이메일 매핑과 로그인을 모두 login-with-id Edge Function이 서버에서 처리하고
	// 세션만 돌려줍니다(이메일이 클라이언트로 노출되지 않음).
	// 성공 처리는 LoginWithEmail과 동일하게 AccessToken/UserId 저장 + LoadPlayerData입니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	void LoginWithId(const FString& Id, const FString& Password);

	// 로그인 후 AccessToken을 이용해서 플레이어 기본 데이터를 불러옵니다.
	// profiles, player_wallets, player_ranks 테이블에 각각 GET 요청을 보냅니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	void LoadPlayerData();

	// 상점 스킨 목록, 내가 보유한 스킨, 현재 장착 스킨 정보를 불러옵니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	void LoadCosmeticData();

	/** Loads cosmetics only when no complete snapshot is cached yet. */
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	void EnsureCosmeticDataLoaded();

	UFUNCTION(BlueprintPure, Category = "Supabase")
	bool HasCosmeticDataSnapshot() const { return bHasCosmeticDataSnapshot; }

	UFUNCTION(BlueprintPure, Category = "Supabase")
	bool IsCosmeticDataLoadInFlight() const { return bCosmeticDataLoadInFlight; }

	UFUNCTION(BlueprintPure, Category = "Supabase")
	bool IsSkinPurchaseInFlight() const { return bSkinPurchaseInFlight; }

	UFUNCTION(BlueprintPure, Category = "Supabase")
	bool IsSkinEquipInFlight() const { return bSkinEquipInFlight; }

	UFUNCTION(BlueprintPure, Category = "Supabase")
	bool IsShopOperationInFlight() const
	{
		return bSkinPurchaseInFlight || bSkinEquipInFlight;
	}

	UFUNCTION(BlueprintPure, Category = "Supabase")
	FString GetPendingShopSetId() const { return PendingShopSetId; }

	// 현재 저장된 Supabase access_token을 반환합니다.
	// 나중에 다른 인증 요청을 보낼 때 사용할 수 있습니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	FString GetAccessToken() const;

	// 로그인한 Supabase 유저 id를 반환합니다.
	// profiles, player_wallets 같은 테이블의 user_id와 연결되는 값입니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	FString GetUserId() const;

	// 서버에서 불러온 닉네임을 반환합니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	FString GetNickname() const;

	// 서버에서 불러온 메타 재화 coin을 반환합니다.
	// GDD 기준 전투 중 총알이 아니라, 상점에서 쓰는 기억 조각입니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	int32 GetCoin() const;

	// 서버에서 불러온 랭크 점수를 반환합니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	int32 GetScore() const;

	// get_leaderboard RPC를 호출해 전체 랭킹(닉네임+점수)을 불러옵니다.
	// 완료되면 OnLeaderboardLoaded를 broadcast합니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	void LoadLeaderboard(int32 Limit = 100);

	// 마지막으로 불러온 리더보드 목록을 반환합니다(점수 내림차순 정렬).
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	TArray<FShowDownRankEntry> GetLeaderboard() const;

	// skin_sets 테이블에서 불러온 활성 상점 상품 목록을 반환합니다.
	// 이름은 기존 코드 호환을 위해 GetShopSkins로 유지합니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	TArray<FShowDownSkin> GetShopSkins() const;

	// player_skins 테이블에서 불러온 실제 보유 스킨 id 목록을 반환합니다.
	// 상점 상품 보유 여부는 IsSkinOwned에서 player_skin_sets까지 함께 확인합니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	TArray<FString> GetOwnedSkinIds() const;

	// 특정 타입에 현재 장착 중인 스킨 id를 반환합니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	FString GetEquippedSkinId(const FString& SkinType) const;

	// Resolves a shop product (skin_sets.id) to the concrete skins.id used by
	// runtime presentation. Returns an empty string when the set has no item
	// for the requested slot.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	FString GetSkinIdForShopSet(
		const FString& SetId,
		const FString& SkinType = TEXT("character")) const;

	// 상점 상품 세트 id 또는 실제 스킨 id를 보유 중인지 확인합니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	bool IsSkinOwned(const FString& SkinId) const;

	// 상점 상품 세트가 현재 장착 상태인지 확인합니다.
	// 세트 안의 모든 slot(card/card_back 등)이 player_equipment와 일치해야 true입니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	bool IsShopItemEquipped(const FString& SetId) const;

	// 보유 중인 상점 상품 세트를 장착합니다.
	// 예: default_card_set -> card와 card_back을 각각 player_equipment에 저장합니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	void EquipSkin(const FString& SkinId);

	// RPC purchase_skin_set을 호출해서 coin 차감과 보유 스킨 추가를 DB에서 한 번에 처리합니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	void PurchaseSkinSet(const FString& SetId);
	
	// 닉네임 변경 결과를 외부에 알리는 이벤트입니다.
	// MainMenu UI가 이 이벤트를 구독해서 화면의 닉네임을 새로고침할 수 있습니다.
	UPROPERTY(BlueprintAssignable)
	FOnNicknameUpdated OnNicknameUpdated;

	// profiles 테이블의 nickname 값을 새 닉네임으로 업데이트합니다.
	// 성공하면 내부 Nickname 변수도 새 값으로 갱신됩니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	void UpdateNickname(const FString& NewNickname);

	// 싱글플레이 승리 보상으로 랭크 점수(10~50)와 코인(50~100)을 지급합니다.
	// 보상 금액은 클라이언트가 정하지 못하도록 서버의 award_win_reward RPC에서 결정합니다.
	// 성공하면 응답의 새 score/coin으로 내부 값을 갱신하고 OnPlayerDataLoaded를 broadcast해
	// UI(메인메뉴 점수/코인 표시)를 새로고침합니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	void AwardWinReward(const FString& MatchId);

	virtual void Deinitialize() override;

	// 마지막 로그인이 서버 응답을 받지 못한(네트워크) 실패였는지 알려줍니다.
	// LoginWidget이 이 값을 보고 "오프라인으로 시작" 버튼을 띄웁니다.
	UFUNCTION(BlueprintPure, Category = "Supabase")
	bool DidLastLoginFailFromNetwork() const { return bLastLoginFailedFromNetwork; }

	// 시연장 네트워크가 로그인 서버를 막을 때를 위한 오프라인 게스트 세션입니다.
	// 토큰이 없으므로 보상/상점/랭킹/멀티플레이는 동작하지 않고, 싱글플레이만 가능합니다.
	UFUNCTION(BlueprintCallable, Category = "Supabase")
	void StartOfflineGuestSession();

	UFUNCTION(BlueprintPure, Category = "Supabase")
	bool IsOfflineGuest() const { return bOfflineGuest; }

	// 개발용 showdown.LoginTest 콘솔 명령이 로그인 결과를 로그로 남길 때 사용합니다.
	UFUNCTION()
	void LogLoginResultForTest(bool bSuccess, const FString& Message);

private:
	bool bAwardWinRewardInFlight = false;
	bool bCosmeticDataLoadInFlight = false;
	bool bCosmeticDataLoadFailed = false;
	bool bHasCosmeticDataSnapshot = false;
	bool bSkinPurchaseInFlight = false;
	bool bSkinEquipInFlight = false;
	bool bLastLoginFailedFromNetwork = false;
	bool bOfflineGuest = false;
	uint64 LoginAttemptGeneration = 0;
	uint64 AuthSessionGeneration = 0;
	uint64 PlayerDataRequestGeneration = 0;
	int32 PendingCosmeticDataRequests = 0;
	TArray<FString> CosmeticDataLoadErrors;
	FString PendingShopSetId;

	// Supabase 프로젝트 기본 URL입니다.
	FString SupabaseUrl = TEXT("https://xfyzrqsbdweckjgxefjr.supabase.co");

	// Public client key. Authorization still uses the signed-in user's access token.
	FString SupabasePublishableKey = TEXT("sb_publishable_CFQn2cVa58AV-iRDqq0QfQ_Hy4rARoV");

	
	// 로그인 성공 시 Supabase가 내려주는 인증 토큰입니다.
	// 이후 DB 요청을 보낼 때 Authorization 헤더에 넣습니다.
	FString AccessToken;

	// 로그인 성공 시 응답에서 가져오는 유저 고유 id입니다.
	FString UserId;

	// access_token은 기본 1시간 뒤 만료됩니다. 시연처럼 게임을 오래 켜 두면
	// 이후 EOS 로그인/보상/상점 요청이 모두 401로 실패하므로, 만료 전에 이 토큰으로 갱신합니다.
	FString RefreshToken;
	FTSTicker::FDelegateHandle TokenRefreshTickerHandle;
	bool bTokenRefreshInFlight = false;

	// profiles 테이블에서 불러온 닉네임입니다.
	FString Nickname;

	// player_wallets 테이블에서 불러온 coin입니다.
	int32 Coin = 0;

	// player_ranks 테이블에서 불러온 score입니다.
	int32 Score = 1000;

	// get_leaderboard로 불러온 전체 랭킹 목록입니다.
	TArray<FShowDownRankEntry> Leaderboard;

	// skin_sets 테이블에서 불러온 활성 상점 상품 목록입니다.
	TArray<FShowDownSkin> ShopSkins;

	// player_skins 테이블에서 불러온 보유 스킨 id 목록입니다.
	TArray<FString> OwnedSkinIds;

	// player_skin_sets 테이블에서 불러온 보유 상품 세트 id 목록입니다.
	TArray<FString> OwnedSkinSetIds;

	// player_equipment 테이블에서 불러온 타입별 장착 스킨 id입니다.
	TMap<FString, FString> EquippedSkinIdsByType;

	// skin_set_items 테이블에서 불러온 세트별 실제 장착 스킨 목록입니다.
	TMap<FString, TArray<FShowDownSkin>> SkinSetItemsBySetId;

	// Cosmetic REST responses are staged here and committed atomically only
	// after all five requests succeed. A failed refresh keeps the last good UI.
	TArray<FShowDownSkin> PendingShopSkins;
	TArray<FString> PendingOwnedSkinIds;
	TArray<FString> PendingOwnedSkinSetIds;
	TMap<FString, FString> PendingEquippedSkinIdsByType;
	TMap<FString, TArray<FShowDownSkin>> PendingSkinSetItemsBySetId;

	// Supabase Auth 로그인 요청의 응답을 처리합니다.
	// access_token과 user.id를 파싱하고, 성공하면 LoadPlayerData를 호출합니다.
	void HandleLoginResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 LoginGeneration);

	// 로그인 서버에 끝내 접속하지 못했을 때, 원인을 현장에서 바로 알 수 있도록
	// 인터넷 연결 / 와이파이 웹 인증 / PC 시간 / 서버 차단 순서로 검사해서 해결 방법을 알려줍니다.
	void DiagnoseLoginNetworkFailure(uint64 LoginGeneration, const FString& FailureReason);

	// login-with-id 요청을 실제로 보냅니다. Attempt는 네트워크 재시도 횟수(0부터)입니다.
	void SendLoginWithIdRequest(const FString& Id, const FString& Password, uint64 LoginGeneration, int32 Attempt);

	// login-with-id Edge Function 응답을 처리합니다.
	// access_token과 user_id를 파싱하고, 성공하면 LoadPlayerData를 호출합니다.
	// 서버에 닿지 못한 실패(연결 오류/타임아웃)는 짧게 기다렸다가 자동으로 다시 시도합니다.
	void HandleLoginWithIdResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 LoginGeneration, FString Id, FString Password, int32 Attempt);

	// profiles 테이블 응답을 처리해서 Nickname 값을 저장합니다.
	void HandleProfileResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration, uint64 PlayerDataGeneration);

	// player_wallets 테이블 응답을 처리해서 Coin 값을 저장합니다.
	void HandleWalletResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration, uint64 PlayerDataGeneration);

	// player_ranks 테이블 응답을 처리해서 Score 값을 저장합니다.
	void HandleRankResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration, uint64 PlayerDataGeneration);

	// get_leaderboard RPC 응답(배열)을 처리해서 Leaderboard에 저장합니다.
	void HandleLeaderboardResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration);

	// skin_sets 테이블 응답을 처리해서 활성 상점 상품 목록을 저장합니다.
	// 함수 이름은 기존 skins 구조에서 이어진 이름입니다.
	void HandleSkinsResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration);

	// player_skins 테이블 응답을 처리해서 보유 스킨 id 목록을 저장합니다.
	void HandlePlayerSkinsResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration);

	// player_skin_sets 테이블 응답을 처리해서 보유 상품 세트 id 목록을 저장합니다.
	void HandlePlayerSkinSetsResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration);

	// skin_set_items 테이블 응답을 처리해서 상품 세트에 포함된 실제 스킨 목록을 저장합니다.
	void HandleSkinSetItemsResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration);

	// player_equipment 테이블 응답을 처리해서 현재 장착 정보를 저장합니다.
	void HandlePlayerEquipmentResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration);

	// All five cosmetic REST calls form one snapshot. This keeps listeners from
	// rebuilding the shop from a partially refreshed cache.
	void CompleteCosmeticDataRequest(bool bSuccess, const FString& Message);
	void BeginAuthenticatedSession();

	// 로그인/갱신 응답의 refresh_token과 expires_in을 저장하고 다음 갱신을 예약합니다.
	// 오프라인 게스트용 상점 목록을 게임에 포함된 캐릭터 카탈로그로 채웁니다.
	void BuildOfflineShopCatalog();

	void StoreSessionTokens(const TSharedPtr<FJsonObject>& SessionObject);
	void ScheduleTokenRefresh(float DelaySeconds);
	void CancelTokenRefresh();
	void RefreshAccessToken();
	void HandleRefreshTokenResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration);

	// AccessToken이 필요한 Supabase REST 요청을 공통으로 만들어주는 helper 함수입니다.
	// apikey, Authorization, Content-Type 헤더를 매번 반복해서 쓰지 않기 위해 분리했습니다.
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> CreateAuthorizedRequest(
		const FString& Endpoint,
		const FString& Verb
	);
	
	// Supabase에 보낸 닉네임 변경 PATCH 요청의 응답을 처리합니다.
	// 성공하면 응답에서 변경된 nickname을 읽어 내부 변수에 저장합니다.
	void HandleUpdateNicknameResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration);

	// award_win_reward RPC 응답({reward, score})을 처리해서 갱신된 Score를 저장합니다.
	void HandleAwardWinRewardResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration);

	// Supabase에 보낸 스킨 장착 PATCH 요청의 응답을 처리합니다.
	void HandleEquipSkinResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration);

	// Supabase RPC purchase_skin_set 응답을 처리합니다.
	void HandlePurchaseSkinSetResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, uint64 RequestGeneration);
};
