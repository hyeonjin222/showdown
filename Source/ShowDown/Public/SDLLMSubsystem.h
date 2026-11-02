#pragma once

#include "CoreMinimal.h"
#include "CollectorAISystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SDLLMSubsystem.generated.h"

class ACard;
class IHttpRequest;
class IHttpResponse;

using FHttpRequestPtr = TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>;
using FHttpResponsePtr = TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>;

USTRUCT(BlueprintType)
struct FSDLLMBossContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	TArray<int32> CollectorHandRanks;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	FString PlayerDialogue;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	FString RecentDialogue;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	FCollectorAISettings CollectorSettings;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	FString RecentRoundHistory;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	FString CurrentRoundActions;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	FString DiscardedCardsSummary;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	FString CurrentPhase = TEXT("none");

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	FString CurrentTurn = TEXT("none");

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	FString ExpectedAction = TEXT("wait");

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	FString OpponentName = TEXT("상대");

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	int32 PlayerForeheadRank = 0;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	int32 CollectorForeheadRank = 0;

	// This server-authored policy stays fixed for the current card round.
	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	FString PlayerCardClaimMode = TEXT("evasive");

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	int32 PlayerCardClaimRank = 0;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	FString PlayerCardClaimDetail = TEXT("evasive");

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	int32 CurrentBet = 1;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	int32 PlayerCommittedBet = 1;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	int32 CollectorCommittedBet = 1;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	int32 PlayerLives = 3;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	int32 CollectorLives = 3;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	int32 RaisesLeft = 0;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	int32 Stage = 1;

	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	int32 Round = 1;

	// 라운드 결과 반응 요청 시 보스(Collector) 관점의 결과: "collector_won" / "collector_lost" / "draw"
	UPROPERTY(BlueprintReadWrite, Category = "ShowDown|LLM")
	FString RoundOutcome;
};

USTRUCT(BlueprintType)
struct FSDLLMBossResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ShowDown|LLM")
	FString Dialogue = TEXT("...");

	UPROPERTY(BlueprintReadOnly, Category = "ShowDown|LLM")
	FString Intent = TEXT("steady");

	UPROPERTY(BlueprintReadOnly, Category = "ShowDown|LLM")
	FCollectorBetDecision Decision;
};

// 무료 등급 모델별 한도(AI Studio > Rate Limit 기준). 게임이 이 한도 안에서만 요청을 보냅니다.
USTRUCT()
struct FSDLLMModelLimit
{
	GENERATED_BODY()

	UPROPERTY(Config)
	FString Model;

	UPROPERTY(Config)
	int32 RPM = 5;

	UPROPERTY(Config)
	int32 RPD = 20;

	UPROPERTY(Config)
	int32 TPM = 250000;
};

DECLARE_DELEGATE_TwoParams(FSDLLMBossResponseCallback, bool /*bSuccess*/, const FSDLLMBossResponse& /*Response*/);
DECLARE_DELEGATE_ThreeParams(FSDLLMBossChatCallback, bool /*bSuccess*/, const FString& /*Dialogue*/, const FString& /*Intent*/);

UCLASS(Config=Game)
class SHOWDOWN_API USDLLMSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	bool bEnableOpenAI = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	FString Model = TEXT("gpt-5.6-terra");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	FString ApiKeyEnvironmentVariable = TEXT("OPENAI_API_KEY");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	float RequestTimeoutSeconds = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	FString BossSpeechStylePrompt = TEXT("Speak in short, natural Korean banmal with the loose swagger of an underground street punk. Always understand and answer the player's latest line directly before adding attitude. For greetings, casual remarks, or neutral questions, respond casually without profanity or hostility. Use mild profanity only rarely and only when the player insults, provokes, threatens, applies strong pressure, or when a tense game result genuinely calls for it. Never insert profanity as filler, never open a reply with unrelated profanity, and never copy stock examples. Keep the confidence relaxed and streetwise rather than constantly angry. No anonymous-board slang, \"ㅋㅋ\", random memes, repetitive catchphrases, hate slurs, sexual insults, threats of real violence, or real-person references.");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	bool bEnableInstantBossChatReply = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	bool bAllowOverlappingBossChatReplies = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	float BossChatReplyCooldownSeconds = 3.0f;

	// "OpenAI"(유료, Responses API) 또는 "Gemini"(무료 등급, OpenAI 호환 Chat Completions)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	FString Provider = TEXT("OpenAI");

	// 앞에서부터 사용하고, 요청이 실패(타임아웃/404 등)하면 다음 대화부터 다음 모델로 넘어갑니다.
	// 무료 한도(429)를 넘긴 모델은 이 실행 동안 다시 쓰지 않습니다.
	// 2026-11-02 무료 등급 하루 한도: Gemma 4 26B 14,400회 / 3.1 Flash Lite 500회 / 3.x Flash 20회.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	TArray<FString> GeminiModelCandidates = { TEXT("gemma-4-26b-a4b-it"), TEXT("gemini-3.1-flash-lite"), TEXT("gemini-3.5-flash") };

	// 모델별 RPM/RPD/TPM. 목록에 없는 모델은 가장 보수적인 값(5/20/250000)을 씁니다.
	UPROPERTY(Config)
	TArray<FSDLLMModelLimit> GeminiModelLimits;

	// 한도의 몇 %까지만 쓸지. 시계 오차나 다른 기기 사용분을 고려해 여유를 둡니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	float QuotaSafetyRatio = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	FString GeminiApiKeyEnvironmentVariable = TEXT("GEMINI_API_KEY");

	// 환경변수가 없을 때 키를 읽을 파일 이름입니다. 게임(프로젝트) 폴더 바로 아래에 둡니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	FString ApiKeyFileName = TEXT("LLMKey.txt");

	// 베팅 결정마다 LLM을 부르면 무료 등급 한도에 금방 닿아 대화까지 느려집니다.
	// false면 베팅은 규칙 기반 AI가 즉시 결정하고, LLM은 채팅/결과 대사에만 씁니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	bool bUseLLMForBetDecisions = false;

	// 키가 없거나 네트워크가 막혀도 보스가 채팅에 대본 대사로 대답하게 합니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "ShowDown|LLM")
	bool bUseScriptedChatFallback = true;

	bool IsConfigured() const;
	bool CanMakeRequests() const;

	/** True when the boss can answer chat at all: via the LLM or the scripted fallback. */
	bool CanReplyToChat() const;
	void RequestBossResponse(const FSDLLMBossContext& Context, FSDLLMBossResponseCallback Callback);
	void RequestBossChatReply(const FSDLLMBossContext& Context, FSDLLMBossChatCallback Callback);
	void RequestBossResultReaction(const FSDLLMBossContext& Context, FSDLLMBossChatCallback Callback);

private:
	bool IsGeminiProvider() const;
	// --- 무료 한도 분배 ---
	struct FGeminiModelUsage
	{
		TArray<TPair<double, int32>> RecentRequests; // (시각, 추정 토큰) - 최근 60초
		int32 DayCount = 0;
		double CooldownUntil = 0.0;
		bool bExhaustedToday = false;
		bool bUnavailable = false;
	};

	FSDLLMModelLimit GetModelLimit(const FString& InModel) const;
	FString GetQuotaDayKey() const;
	void LoadQuotaUsageIfNeeded() const;
	void SaveQuotaUsage() const;
	// 한도 안에서 보낼 수 있는 첫 후보 모델. 없으면 빈 문자열(→ 대본/규칙 AI로 처리).
	FString SelectGeminiModel(int32 EstimatedTokens) const;
	void RecordGeminiRequest(const FString& InModel, int32 EstimatedTokens);
	void HandleGeminiFailure(const FString& InModel, int32 ResponseCode, const FString& ResponseBody);

	mutable TMap<FString, FGeminiModelUsage> GeminiUsage;
	mutable FString QuotaDayKey;
	mutable bool bQuotaUsageLoaded = false;

	// 무료 등급에서 요청이 겹치면 서로 느려지므로 동시에 하나만 보냅니다.
	int32 InFlightRequests = 0;
	FString ResolveApiKey() const;

	// Builds an HTTP request for the configured provider from a Responses-API style body.
	// Returns null when every Gemini model is at its free-tier limit; OutModel names the chosen model.
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> CreateProviderRequest(const FString& ResponsesBody, FString& OutModel);
	FString ConvertToChatCompletionsBody(const FString& ResponsesBody, const FString& InModel) const;

	// 채팅 대답을 실제로 보냅니다. 과부하/타임아웃이면 같은 대사를 다른 모델로 바로 다시 보냅니다.
	void SendBossChatReply(const FSDLLMBossContext& Context, FSDLLMBossChatCallback Callback, int32 Attempt);

	FString PickScriptedChatReply(const FSDLLMBossContext& Context) const;
	void DeliverScriptedChatReply(const FSDLLMBossContext& Context, FSDLLMBossChatCallback Callback);
	mutable int32 ScriptedLineCursor = 0;
	FString BuildPrompt(const FSDLLMBossContext& Context) const;
	FString BuildRequestBody(const FSDLLMBossContext& Context) const;
	FString BuildChatReplyRequestBody(const FSDLLMBossContext& Context) const;
	FString BuildResultReactionRequestBody(const FSDLLMBossContext& Context) const;
	bool ParseBossResponse(const FString& ResponseBody, FSDLLMBossResponse& OutResponse) const;
	bool ParseBossChatReply(const FString& ResponseBody, FString& OutDialogue, FString& OutIntent) const;
	FString ExtractOutputText(const TSharedPtr<FJsonObject>& RootObject) const;
	void ClampResponse(FSDLLMBossResponse& Response) const;
};
