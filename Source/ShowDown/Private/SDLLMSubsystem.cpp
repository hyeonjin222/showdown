#include "SDLLMSubsystem.h"

#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HttpModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// 로컬 키 파일(.gitignore 등록). 없으면 환경변수만 사용한다.
#if __has_include("SDLLMSecrets.h")
#include "SDLLMSecrets.h"
#endif

#if !UE_BUILD_SHIPPING
// 개발 빌드 전용: showdown.ChatTest 안녕 → 보스 채팅 응답(LLM 또는 대본)을 로그로 확인합니다.
static FAutoConsoleCommandWithWorldAndArgs GShowDownChatTestCommand(
	TEXT("showdown.ChatTest"),
	TEXT("showdown.ChatTest <line> : asks the boss chat reply path and logs the answer."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USDLLMSubsystem* LLM = GameInstance ? GameInstance->GetSubsystem<USDLLMSubsystem>() : nullptr;
		if (!LLM)
		{
			return;
		}

		FSDLLMBossContext Context;
		Context.PlayerDialogue = FString::Join(Args, TEXT(" "));
		UE_LOG(LogTemp, Display, TEXT("[ChatTest] provider=%s configured=%d canReply=%d line=%s"),
			*LLM->Provider, LLM->IsConfigured() ? 1 : 0, LLM->CanReplyToChat() ? 1 : 0, *Context.PlayerDialogue);
		LLM->RequestBossChatReply(Context, FSDLLMBossChatCallback::CreateLambda(
			[](bool bSuccess, const FString& Dialogue, const FString& Intent)
			{
				UE_LOG(LogTemp, Display, TEXT("[ChatTest] success=%d reply=%s intent=%s"), bSuccess ? 1 : 0, *Dialogue, *Intent);
			}));
	}));
#endif

bool USDLLMSubsystem::IsConfigured() const
{
	if (!bEnableOpenAI || ResolveApiKey().IsEmpty())
	{
		return false;
	}
	// 지금 한도 안에서 보낼 수 있는 모델이 없으면 요청 자체를 하지 않습니다(오류 대신 대본/규칙 AI).
	return !IsGeminiProvider() || !SelectGeminiModel(0).IsEmpty();
}

bool USDLLMSubsystem::CanMakeRequests() const
{
	const UWorld* World = GetWorld();
	return IsConfigured() && World && World->GetNetMode() == NM_Standalone;
}

bool USDLLMSubsystem::CanReplyToChat() const
{
	const UWorld* World = GetWorld();
	const bool bStandalone = World && World->GetNetMode() == NM_Standalone;
	return bStandalone && (IsConfigured() || bUseScriptedChatFallback);
}

bool USDLLMSubsystem::IsGeminiProvider() const
{
	return Provider.Equals(TEXT("Gemini"), ESearchCase::IgnoreCase);
}

FSDLLMModelLimit USDLLMSubsystem::GetModelLimit(const FString& InModel) const
{
	for (const FSDLLMModelLimit& Limit : GeminiModelLimits)
	{
		if (Limit.Model.Equals(InModel, ESearchCase::IgnoreCase))
		{
			return Limit;
		}
	}
	FSDLLMModelLimit Conservative;
	Conservative.Model = InModel;
	return Conservative;
}

FString USDLLMSubsystem::GetQuotaDayKey() const
{
	// 무료 한도는 미국 태평양 자정에 초기화됩니다(11월 기준 UTC-8).
	return (FDateTime::UtcNow() - FTimespan::FromHours(8.0)).ToString(TEXT("%Y-%m-%d"));
}

void USDLLMSubsystem::LoadQuotaUsageIfNeeded() const
{
	const FString Today = GetQuotaDayKey();
	if (bQuotaUsageLoaded && QuotaDayKey == Today)
	{
		return;
	}

	// 날짜가 바뀌면 하루 사용량을 비웁니다. 같은 날이면 지난 실행에서 쓴 횟수를 이어서 셉니다.
	GeminiUsage.Reset();
	QuotaDayKey = Today;
	bQuotaUsageLoaded = true;

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LLMQuotaUsage.json"))))
	{
		return;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	FString SavedDay;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()
		|| !Root->TryGetStringField(TEXT("day"), SavedDay) || SavedDay != Today)
	{
		return;
	}

	const TSharedPtr<FJsonObject>* Models = nullptr;
	if (Root->TryGetObjectField(TEXT("models"), Models))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Models)->Values)
		{
			const TSharedPtr<FJsonObject> ModelObject = Entry.Value->AsObject();
			if (!ModelObject.IsValid())
			{
				continue;
			}
			FGeminiModelUsage& Usage = GeminiUsage.FindOrAdd(Entry.Key);
			Usage.DayCount = ModelObject->GetIntegerField(TEXT("count"));
			ModelObject->TryGetBoolField(TEXT("exhausted"), Usage.bExhaustedToday);
		}
	}
}

void USDLLMSubsystem::SaveQuotaUsage() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("day"), QuotaDayKey);
	TSharedRef<FJsonObject> Models = MakeShared<FJsonObject>();
	for (const TPair<FString, FGeminiModelUsage>& Entry : GeminiUsage)
	{
		TSharedRef<FJsonObject> ModelObject = MakeShared<FJsonObject>();
		ModelObject->SetNumberField(TEXT("count"), Entry.Value.DayCount);
		ModelObject->SetBoolField(TEXT("exhausted"), Entry.Value.bExhaustedToday);
		Models->SetObjectField(Entry.Key, ModelObject);
	}
	Root->SetObjectField(TEXT("models"), Models);

	FString Text;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(Root, Writer);
	FFileHelper::SaveStringToFile(Text, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LLMQuotaUsage.json")));
}

FString USDLLMSubsystem::SelectGeminiModel(const int32 EstimatedTokens) const
{
	LoadQuotaUsageIfNeeded();

	const double Now = FPlatformTime::Seconds();
	const float Ratio = FMath::Clamp(QuotaSafetyRatio, 0.1f, 1.0f);
	for (const FString& CandidateModel : GeminiModelCandidates)
	{
		FGeminiModelUsage& Usage = GeminiUsage.FindOrAdd(CandidateModel);
		if (Usage.bUnavailable || Usage.bExhaustedToday || Now < Usage.CooldownUntil)
		{
			continue;
		}

		const FSDLLMModelLimit Limit = GetModelLimit(CandidateModel);
		Usage.RecentRequests.RemoveAll([Now](const TPair<double, int32>& Request) { return Now - Request.Key >= 60.0; });

		int32 TokensLastMinute = 0;
		for (const TPair<double, int32>& Request : Usage.RecentRequests)
		{
			TokensLastMinute += Request.Value;
		}

		const int32 MaxPerMinute = FMath::Max(1, FMath::FloorToInt(Limit.RPM * Ratio));
		const int32 MaxPerDay = FMath::Max(1, FMath::FloorToInt(Limit.RPD * Ratio));
		const int32 MaxTokensPerMinute = FMath::FloorToInt(Limit.TPM * Ratio);
		if (Usage.RecentRequests.Num() < MaxPerMinute
			&& Usage.DayCount < MaxPerDay
			&& TokensLastMinute + EstimatedTokens <= MaxTokensPerMinute)
		{
			return CandidateModel;
		}
	}
	return FString();
}

void USDLLMSubsystem::RecordGeminiRequest(const FString& InModel, const int32 EstimatedTokens)
{
	FGeminiModelUsage& Usage = GeminiUsage.FindOrAdd(InModel);
	Usage.RecentRequests.Emplace(FPlatformTime::Seconds(), EstimatedTokens);
	++Usage.DayCount;
	SaveQuotaUsage();
}

void USDLLMSubsystem::HandleGeminiFailure(const FString& InModel, const int32 ResponseCode, const FString& ResponseBody)
{
	if (InModel.IsEmpty())
	{
		return;
	}

	FGeminiModelUsage& Usage = GeminiUsage.FindOrAdd(InModel);
	if (ResponseCode == 429)
	{
		// 하루 한도 초과면 오늘은 제외, 분당 한도 초과면 1분만 쉽니다.
		if (ResponseBody.Contains(TEXT("per day"), ESearchCase::IgnoreCase) || ResponseBody.Contains(TEXT("PerDay")))
		{
			Usage.bExhaustedToday = true;
			SaveQuotaUsage();
			UE_LOG(LogTemp, Warning, TEXT("Gemini model %s: daily free quota used up; skipped until reset."), *InModel);
		}
		else
		{
			Usage.CooldownUntil = FPlatformTime::Seconds() + 60.0;
			UE_LOG(LogTemp, Warning, TEXT("Gemini model %s: per-minute limit hit; cooling down 60s."), *InModel);
		}
	}
	else if (ResponseCode == 404)
	{
		Usage.bUnavailable = true;
		UE_LOG(LogTemp, Warning, TEXT("Gemini model %s is not available for this key; skipped."), *InModel);
	}
	else if (ResponseCode == 0 || ResponseCode >= 500)
	{
		// 타임아웃/과부하는 잠깐 다른 모델로 돌립니다.
		Usage.CooldownUntil = FPlatformTime::Seconds() + 30.0;
		UE_LOG(LogTemp, Warning, TEXT("Gemini model %s timed out or overloaded (HTTP %d); cooling down 30s."), *InModel, ResponseCode);
	}
}

TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> USDLLMSubsystem::CreateProviderRequest(const FString& ResponsesBody, FString& OutModel)
{
	OutModel.Reset();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ResolveApiKey()));
	Request->SetTimeout(RequestTimeoutSeconds);

	if (IsGeminiProvider())
	{
		// 한국어가 섞인 프롬프트는 대략 2글자당 1토큰 이상이므로 넉넉히 잡고, 출력분 300토큰을 더합니다.
		const int32 EstimatedTokens = ResponsesBody.Len() / 2 + 300;
		OutModel = SelectGeminiModel(EstimatedTokens);
		if (OutModel.IsEmpty())
		{
			UE_LOG(LogTemp, Log, TEXT("All Gemini models are at their free-tier limits right now; using fallback."));
			return nullptr;
		}
		RecordGeminiRequest(OutModel, EstimatedTokens);
		UE_LOG(LogTemp, Log, TEXT("LLM request -> %s (~%d tokens, today %d)"),
			*OutModel, EstimatedTokens, GeminiUsage.FindOrAdd(OutModel).DayCount);

		// Gemini 무료 등급은 OpenAI 호환 Chat Completions 엔드포인트를 제공합니다.
		Request->SetURL(TEXT("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"));
		Request->SetContentAsString(ConvertToChatCompletionsBody(ResponsesBody, OutModel));
	}
	else
	{
		Request->SetURL(TEXT("https://api.openai.com/v1/responses"));
		Request->SetContentAsString(ResponsesBody);
	}
	return Request;
}

FString USDLLMSubsystem::ConvertToChatCompletionsBody(const FString& ResponsesBody, const FString& InModel) const
{
	// 기존 프롬프트/스키마 빌더는 Responses API 형식이라, 같은 내용을 Chat Completions 형식으로 옮깁니다.
	TSharedPtr<FJsonObject> Source;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponsesBody);
	if (!FJsonSerializer::Deserialize(Reader, Source) || !Source.IsValid())
	{
		return ResponsesBody;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), InModel);
	// 짧은 대사만 필요하므로 Gemini는 사고(thinking) 토큰을 끄고 응답 시간을 줄입니다.
	// Gemma는 이 옵션을 받지 않아(요청 거부) 보내지 않습니다.
	if (!InModel.StartsWith(TEXT("gemma"), ESearchCase::IgnoreCase))
	{
		Root->SetStringField(TEXT("reasoning_effort"), TEXT("none"));
	}

	double MaxTokens = 160.0;
	Source->TryGetNumberField(TEXT("max_output_tokens"), MaxTokens);
	Root->SetNumberField(TEXT("max_tokens"), FMath::Max(MaxTokens, 256.0));

	TArray<TSharedPtr<FJsonValue>> Messages;
	const TArray<TSharedPtr<FJsonValue>>* InputMessages = nullptr;
	if (Source->TryGetArrayField(TEXT("input"), InputMessages))
	{
		for (const TSharedPtr<FJsonValue>& Value : *InputMessages)
		{
			const TSharedPtr<FJsonObject> Message = Value->AsObject();
			if (!Message.IsValid())
			{
				continue;
			}

			FString Role;
			FString Content;
			Message->TryGetStringField(TEXT("role"), Role);
			Message->TryGetStringField(TEXT("content"), Content);

			TSharedRef<FJsonObject> Converted = MakeShared<FJsonObject>();
			Converted->SetStringField(TEXT("role"), Role == TEXT("developer") ? TEXT("system") : Role);
			Converted->SetStringField(TEXT("content"), Content);
			Messages.Add(MakeShared<FJsonValueObject>(Converted));
		}
	}
	Root->SetArrayField(TEXT("messages"), Messages);

	const TSharedPtr<FJsonObject>* TextObject = nullptr;
	const TSharedPtr<FJsonObject>* FormatObject = nullptr;
	const TSharedPtr<FJsonObject>* SchemaObject = nullptr;
	if (Source->TryGetObjectField(TEXT("text"), TextObject)
		&& (*TextObject)->TryGetObjectField(TEXT("format"), FormatObject)
		&& (*FormatObject)->TryGetObjectField(TEXT("schema"), SchemaObject))
	{
		// Gemini는 additionalProperties를 지원하지 않으므로 빼고 보냅니다.
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>(**SchemaObject);
		Schema->RemoveField(TEXT("additionalProperties"));

		FString SchemaName = TEXT("showdown_response");
		(*FormatObject)->TryGetStringField(TEXT("name"), SchemaName);

		TSharedRef<FJsonObject> JsonSchema = MakeShared<FJsonObject>();
		JsonSchema->SetStringField(TEXT("name"), SchemaName);
		JsonSchema->SetObjectField(TEXT("schema"), Schema);

		TSharedRef<FJsonObject> ResponseFormat = MakeShared<FJsonObject>();
		ResponseFormat->SetStringField(TEXT("type"), TEXT("json_schema"));
		ResponseFormat->SetObjectField(TEXT("json_schema"), JsonSchema);
		Root->SetObjectField(TEXT("response_format"), ResponseFormat);
	}

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

FString USDLLMSubsystem::PickScriptedChatReply(const FSDLLMBossContext& Context) const
{
	// 키/네트워크 없이도 보스가 플레이어 말에 반응하도록 키워드별 대본을 고릅니다.
	// 이마 카드는 절대 알려주지 않는다는 규칙은 대본에서도 지킵니다.
	const FString Line = Context.PlayerDialogue.ToLower();
	auto ContainsAny = [&Line](std::initializer_list<const TCHAR*> Words)
	{
		for (const TCHAR* Word : Words)
		{
			if (Line.Contains(Word))
			{
				return true;
			}
		}
		return false;
	};

	static const TArray<FString> GreetingLines = {
		TEXT("어, 왔냐. 앉아."),
		TEXT("인사는 됐고, 카드나 봐."),
		TEXT("반갑긴. 판이나 시작하자.") };
	static const TArray<FString> CardProbeLines = {
		TEXT("알려줄 리가 있냐."),
		TEXT("궁금하면 걸어 보든가."),
		TEXT("답은 네 이마에 있잖아."),
		TEXT("내 입으로는 말 못 하지.") };
	static const TArray<FString> ProvokeLines = {
		TEXT("말 조심해. 판 아직 안 끝났다."),
		TEXT("입만 살았네."),
		TEXT("그 말, 카드로 증명해 봐.") };
	static const TArray<FString> BetLines = {
		TEXT("걸 거면 시원하게 걸어."),
		TEXT("쫄았냐? 손이 떨리는데."),
		TEXT("좋아, 어디까지 가나 보자.") };
	static const TArray<FString> QuestionLines = {
		TEXT("글쎄, 판 보면 알겠지."),
		TEXT("그건 네가 알아서 해."),
		TEXT("질문은 이긴 다음에 해.") };
	static const TArray<FString> DefaultLines = {
		TEXT("그래서, 할 거야 말 거야?"),
		TEXT("말은 많은데 손은 느리네."),
		TEXT("집중해. 판돈 걸려 있다."),
		TEXT("흠. 계속해 봐.") };

	const TArray<FString>* Pool = &DefaultLines;
	if (ContainsAny({ TEXT("카드"), TEXT("숫자"), TEXT("몇"), TEXT("이마"), TEXT("패"), TEXT("뭐 줬"), TEXT("card") }))
	{
		Pool = &CardProbeLines;
	}
	else if (ContainsAny({ TEXT("바보"), TEXT("멍청"), TEXT("꺼져"), TEXT("죽"), TEXT("시발"), TEXT("씨발"), TEXT("병신"), TEXT("못생"), TEXT("짜증") }))
	{
		Pool = &ProvokeLines;
	}
	else if (ContainsAny({ TEXT("안녕"), TEXT("하이"), TEXT("반가"), TEXT("ㅎㅇ"), TEXT("hello"), TEXT("hi") }))
	{
		Pool = &GreetingLines;
	}
	else if (ContainsAny({ TEXT("올려"), TEXT("레이즈"), TEXT("콜"), TEXT("다이"), TEXT("폴드"), TEXT("베팅"), TEXT("걸"), TEXT("raise"), TEXT("call"), TEXT("fold") }))
	{
		Pool = &BetLines;
	}
	else if (ContainsAny({ TEXT("?"), TEXT("왜"), TEXT("뭐"), TEXT("어떻게"), TEXT("누구") }))
	{
		Pool = &QuestionLines;
	}

	return (*Pool)[(ScriptedLineCursor++) % Pool->Num()];
}

void USDLLMSubsystem::DeliverScriptedChatReply(const FSDLLMBossContext& Context, FSDLLMBossChatCallback Callback)
{
	const FString Reply = PickScriptedChatReply(Context);

	// 바로 답하면 기계처럼 보이므로 생각하는 듯한 짧은 텀을 둡니다.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(
		this,
		[Callback, Reply](float)
		{
			Callback.ExecuteIfBound(true, Reply, TEXT("steady"));
			return false;
		}), 0.7f);
}

void USDLLMSubsystem::RequestBossResponse(const FSDLLMBossContext& Context, FSDLLMBossResponseCallback Callback)
{
	if (!CanMakeRequests())
	{
		Callback.ExecuteIfBound(false, FSDLLMBossResponse());
		return;
	}

	FString RequestModel;
	const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request = CreateProviderRequest(BuildRequestBody(Context), RequestModel);
	if (!Request.IsValid())
	{
		Callback.ExecuteIfBound(false, FSDLLMBossResponse());
		return;
	}

	Request->OnProcessRequestComplete().BindWeakLambda(
		this,
		[this, Callback, RequestModel](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bWasSuccessful)
		{
			InFlightRequests = FMath::Max(0, InFlightRequests - 1);
			FSDLLMBossResponse ParsedResponse;
			const bool bHttpOk = bWasSuccessful
				&& ResponsePtr.IsValid()
				&& EHttpResponseCodes::IsOk(ResponsePtr->GetResponseCode());

			const bool bParsed = bHttpOk && ParseBossResponse(ResponsePtr->GetContentAsString(), ParsedResponse);
			if (!bParsed)
			{
				const int32 ResponseCode = ResponsePtr.IsValid() ? ResponsePtr->GetResponseCode() : 0;
				const FString ResponsePreview = ResponsePtr.IsValid()
					? ResponsePtr->GetContentAsString().Left(512)
					: TEXT("No HTTP response");
				UE_LOG(LogTemp, Warning, TEXT("LLM boss response failed (%s). HTTP %d. Body: %s"), *RequestModel, ResponseCode, *ResponsePreview);
				HandleGeminiFailure(RequestModel, ResponseCode, ResponsePreview);
			}

			Callback.ExecuteIfBound(bParsed, ParsedResponse);
		});

	++InFlightRequests;
	if (!Request->ProcessRequest())
	{
		InFlightRequests = FMath::Max(0, InFlightRequests - 1);
		Callback.ExecuteIfBound(false, FSDLLMBossResponse());
	}
}

void USDLLMSubsystem::RequestBossChatReply(const FSDLLMBossContext& Context, FSDLLMBossChatCallback Callback)
{
	if (!CanMakeRequests())
	{
		if (CanReplyToChat())
		{
			DeliverScriptedChatReply(Context, Callback);
			return;
		}
		Callback.ExecuteIfBound(false, FString(), FString());
		return;
	}

	SendBossChatReply(Context, Callback, 0);
}

void USDLLMSubsystem::SendBossChatReply(const FSDLLMBossContext& Context, FSDLLMBossChatCallback Callback, const int32 Attempt)
{
	FString RequestModel;
	const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request = CreateProviderRequest(BuildChatReplyRequestBody(Context), RequestModel);
	if (!Request.IsValid())
	{
		// 모든 모델이 한도/과부하 대기 중이면 오류를 내지 않고 바로 대본으로 대답합니다.
		DeliverScriptedChatReply(Context, Callback);
		return;
	}

	Request->OnProcessRequestComplete().BindWeakLambda(
		this,
		[this, Callback, Context, RequestModel, Attempt](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bWasSuccessful)
		{
			InFlightRequests = FMath::Max(0, InFlightRequests - 1);
			FString Dialogue;
			FString Intent;
			const bool bHttpOk = bWasSuccessful
				&& ResponsePtr.IsValid()
				&& EHttpResponseCodes::IsOk(ResponsePtr->GetResponseCode());

			const bool bParsed = bHttpOk && ParseBossChatReply(ResponsePtr->GetContentAsString(), Dialogue, Intent);
			if (!bParsed)
			{
				const int32 ResponseCode = ResponsePtr.IsValid() ? ResponsePtr->GetResponseCode() : 0;
				const FString ResponsePreview = ResponsePtr.IsValid()
					? ResponsePtr->GetContentAsString().Left(512)
					: TEXT("No HTTP response");
				UE_LOG(LogTemp, Warning, TEXT("LLM boss chat reply failed (%s). HTTP %d. Body: %s"), *RequestModel, ResponseCode, *ResponsePreview);
				HandleGeminiFailure(RequestModel, ResponseCode, ResponsePreview);

				// 무료 등급 과부하(5xx)/한도(429)/모델 없음(404)은 1~2초 안에 돌아오므로 다른 모델로 바로 다시 보냅니다.
				// 타임아웃은 이미 오래 기다렸으므로 재시도하지 않고 대본으로 넘어갑니다.
				// 실패한 모델은 HandleGeminiFailure에서 잠시 제외됐으므로 다음 후보가 선택됩니다.
				const bool bRetryable = ResponseCode == 404 || ResponseCode == 429 || ResponseCode >= 500;
				if (IsGeminiProvider() && bRetryable && Attempt + 1 < FMath::Max(1, GeminiModelCandidates.Num()))
				{
					SendBossChatReply(Context, Callback, Attempt + 1);
					return;
				}

				// 네트워크가 막히거나 무료 사용량을 넘겨도 대화가 끊기지 않도록 대본으로 이어 갑니다.
				if (bUseScriptedChatFallback)
				{
					DeliverScriptedChatReply(Context, Callback);
					return;
				}
			}

			Callback.ExecuteIfBound(bParsed, Dialogue, Intent);
		});

	++InFlightRequests;
	if (!Request->ProcessRequest())
	{
		InFlightRequests = FMath::Max(0, InFlightRequests - 1);
		if (bUseScriptedChatFallback)
		{
			DeliverScriptedChatReply(Context, Callback);
			return;
		}
		Callback.ExecuteIfBound(false, FString(), FString());
	}
}

void USDLLMSubsystem::RequestBossResultReaction(const FSDLLMBossContext& Context, FSDLLMBossChatCallback Callback)
{
	// 다른 요청이 진행 중이면 기다리지 않고 호출 측의 고정 결과 대사를 쓰게 합니다.
	if (!CanMakeRequests() || InFlightRequests > 0)
	{
		Callback.ExecuteIfBound(false, FString(), FString());
		return;
	}

	FString RequestModel;
	const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request = CreateProviderRequest(BuildResultReactionRequestBody(Context), RequestModel);
	if (!Request.IsValid())
	{
		Callback.ExecuteIfBound(false, FString(), FString());
		return;
	}

	Request->OnProcessRequestComplete().BindWeakLambda(
		this,
		[this, Callback, RequestModel](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bWasSuccessful)
		{
			InFlightRequests = FMath::Max(0, InFlightRequests - 1);
			FString Dialogue;
			FString Intent;
			const bool bHttpOk = bWasSuccessful
				&& ResponsePtr.IsValid()
				&& EHttpResponseCodes::IsOk(ResponsePtr->GetResponseCode());

			const bool bParsed = bHttpOk && ParseBossChatReply(ResponsePtr->GetContentAsString(), Dialogue, Intent);
			if (!bParsed)
			{
				const int32 ResponseCode = ResponsePtr.IsValid() ? ResponsePtr->GetResponseCode() : 0;
				const FString ResponsePreview = ResponsePtr.IsValid()
					? ResponsePtr->GetContentAsString().Left(512)
					: TEXT("No HTTP response");
				UE_LOG(LogTemp, Warning, TEXT("LLM boss result reaction failed (%s). HTTP %d. Body: %s"), *RequestModel, ResponseCode, *ResponsePreview);
				HandleGeminiFailure(RequestModel, ResponseCode, ResponsePreview);
			}

			Callback.ExecuteIfBound(bParsed, Dialogue, Intent);
		});

	++InFlightRequests;
	if (!Request->ProcessRequest())
	{
		InFlightRequests = FMath::Max(0, InFlightRequests - 1);
		Callback.ExecuteIfBound(false, FString(), FString());
	}
}

FString USDLLMSubsystem::ResolveApiKey() const
{
	if (IsGeminiProvider())
	{
		// 시연 PC에서는 환경변수 대신 게임 폴더의 키 파일을 두는 편이 간단합니다.
		FString GeminiKey = FPlatformMisc::GetEnvironmentVariable(*GeminiApiKeyEnvironmentVariable).TrimStartAndEnd();
		if (GeminiKey.IsEmpty() && !ApiKeyFileName.IsEmpty())
		{
			FString FileKey;
			if (FFileHelper::LoadFileToString(FileKey, *FPaths::Combine(FPaths::ProjectDir(), ApiKeyFileName)))
			{
				GeminiKey = FileKey.TrimStartAndEnd();
			}
		}
		return GeminiKey;
	}

	FString Key = FPlatformMisc::GetEnvironmentVariable(*ApiKeyEnvironmentVariable).TrimStartAndEnd();

#ifdef SHOWDOWN_LLM_API_KEY
	// 환경변수가 비어 있으면 로컬 키 파일(SDLLMSecrets.h)의 값을 사용한다.
	if (Key.IsEmpty())
	{
		Key = FString(SHOWDOWN_LLM_API_KEY).TrimStartAndEnd();
	}
#endif

	return Key;
}

FString USDLLMSubsystem::BuildPrompt(const FSDLLMBossContext& Context) const
{
	FString HandRanksText;
	for (int32 Index = 0; Index < Context.CollectorHandRanks.Num(); ++Index)
	{
		if (Index > 0)
		{
			HandRanksText += TEXT(", ");
		}
		HandRanksText += FString::FromInt(Context.CollectorHandRanks[Index]);
	}

	return FString::Printf(
		TEXT("You are the Collector, a quiet analog-horror card boss in ShowDown. ")
		TEXT("Speech style: %s ")
		TEXT("Talk with the player, then directly choose your betting action. ")
		TEXT("Your personality must follow these configured values: low_card_bias=%.2f, bluff_rate=%.2f, aggression=%.2f, fold_tendency=%.2f, noise=%.2f. ")
		TEXT("High low_card_bias means you prefer giving the player low cards and behave as if the player is under pressure. ")
		TEXT("High bluff_rate means you may raise even from uncertain positions. High aggression means larger raises. High fold_tendency means folding earlier when pressured. ")
		TEXT("Use the round memory and action log to adapt to player habits, previous wins/losses, previous exchanged cards, life pressure, and repeated aggression. ")
		TEXT("Legal actions are check, call, raise, fold. If you raise, choose target_bet from current_bet+1 to 6. Keep Korean dialogue under 32 characters. ")
		TEXT("Recent chat:\n%s\nRecent rounds:\n%s\nCurrent round actions:\n%s\nDiscarded/seen cards: %s\n")
		TEXT("Context: stage=%d, round=%d, player_lives=%d, collector_lives=%d, collector_hand=[%s], player_forehead_rank=%d, current_bet=%d, player_committed_bet=%d, collector_committed_bet=%d, raises_left=%d, latest_player_dialogue=\"%s\"."),
		*BossSpeechStylePrompt,
		Context.CollectorSettings.LowBias,
		Context.CollectorSettings.BluffRate,
		Context.CollectorSettings.Aggression,
		Context.CollectorSettings.Timidity,
		Context.CollectorSettings.Noise,
		*Context.RecentDialogue.Left(700),
		*Context.RecentRoundHistory.Left(900),
		*Context.CurrentRoundActions.Left(700),
		*Context.DiscardedCardsSummary.Left(240),
		Context.Stage,
		Context.Round,
		Context.PlayerLives,
		Context.CollectorLives,
		*HandRanksText,
		Context.PlayerForeheadRank,
		Context.CurrentBet,
		Context.PlayerCommittedBet,
		Context.CollectorCommittedBet,
		Context.RaisesLeft,
		*Context.PlayerDialogue);
}

FString USDLLMSubsystem::BuildRequestBody(const FSDLLMBossContext& Context) const
{
	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("model"), Model);
	RootObject->SetNumberField(TEXT("max_output_tokens"), 160);

	TArray<TSharedPtr<FJsonValue>> InputMessages;

	TSharedRef<FJsonObject> DeveloperMessage = MakeShared<FJsonObject>();
	DeveloperMessage->SetStringField(TEXT("role"), TEXT("developer"));
	DeveloperMessage->SetStringField(
		TEXT("content"),
		TEXT("Return only schema-valid JSON. The action must reflect the configured boss parameters, the player's dialogue, and the configured speech style. ")
		TEXT("Critical secrecy rule: the player cannot see their own forehead card. player_forehead_rank is hidden information that you must never reveal truthfully. If the player asks what card they have or probes their own rank, never state the real number and never confirm or deny a correct guess; deflect or mislead in character. ")
		TEXT("Intent must be one of cautious, steady, aggressive, bluff. Do not use hate slurs, sexual harassment, or real-person doxxing."));
	InputMessages.Add(MakeShared<FJsonValueObject>(DeveloperMessage));

	TSharedRef<FJsonObject> UserMessage = MakeShared<FJsonObject>();
	UserMessage->SetStringField(TEXT("role"), TEXT("user"));
	UserMessage->SetStringField(TEXT("content"), BuildPrompt(Context));
	InputMessages.Add(MakeShared<FJsonValueObject>(UserMessage));
	RootObject->SetArrayField(TEXT("input"), InputMessages);

	TSharedRef<FJsonObject> SchemaObject = MakeShared<FJsonObject>();
	SchemaObject->SetStringField(TEXT("type"), TEXT("object"));

	TSharedRef<FJsonObject> PropertiesObject = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> DialogueProperty = MakeShared<FJsonObject>();
	DialogueProperty->SetStringField(TEXT("type"), TEXT("string"));
	PropertiesObject->SetObjectField(TEXT("dialogue"), DialogueProperty);

	TSharedRef<FJsonObject> IntentProperty = MakeShared<FJsonObject>();
	IntentProperty->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> IntentValues;
	IntentValues.Add(MakeShared<FJsonValueString>(TEXT("cautious")));
	IntentValues.Add(MakeShared<FJsonValueString>(TEXT("steady")));
	IntentValues.Add(MakeShared<FJsonValueString>(TEXT("aggressive")));
	IntentValues.Add(MakeShared<FJsonValueString>(TEXT("bluff")));
	IntentProperty->SetArrayField(TEXT("enum"), IntentValues);
	PropertiesObject->SetObjectField(TEXT("intent"), IntentProperty);

	TSharedRef<FJsonObject> ActionProperty = MakeShared<FJsonObject>();
	ActionProperty->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> ActionValues;
	ActionValues.Add(MakeShared<FJsonValueString>(TEXT("check")));
	ActionValues.Add(MakeShared<FJsonValueString>(TEXT("call")));
	ActionValues.Add(MakeShared<FJsonValueString>(TEXT("raise")));
	ActionValues.Add(MakeShared<FJsonValueString>(TEXT("fold")));
	ActionProperty->SetArrayField(TEXT("enum"), ActionValues);
	PropertiesObject->SetObjectField(TEXT("action"), ActionProperty);

	TSharedRef<FJsonObject> TargetBetProperty = MakeShared<FJsonObject>();
	TargetBetProperty->SetStringField(TEXT("type"), TEXT("integer"));
	TargetBetProperty->SetNumberField(TEXT("minimum"), 0);
	TargetBetProperty->SetNumberField(TEXT("maximum"), 6);
	PropertiesObject->SetObjectField(TEXT("target_bet"), TargetBetProperty);
	SchemaObject->SetObjectField(TEXT("properties"), PropertiesObject);

	TArray<TSharedPtr<FJsonValue>> RequiredFields;
	RequiredFields.Add(MakeShared<FJsonValueString>(TEXT("dialogue")));
	RequiredFields.Add(MakeShared<FJsonValueString>(TEXT("intent")));
	RequiredFields.Add(MakeShared<FJsonValueString>(TEXT("action")));
	RequiredFields.Add(MakeShared<FJsonValueString>(TEXT("target_bet")));
	SchemaObject->SetArrayField(TEXT("required"), RequiredFields);
	SchemaObject->SetBoolField(TEXT("additionalProperties"), false);

	TSharedRef<FJsonObject> FormatObject = MakeShared<FJsonObject>();
	FormatObject->SetStringField(TEXT("type"), TEXT("json_schema"));
	FormatObject->SetStringField(TEXT("name"), TEXT("showdown_boss_response"));
	FormatObject->SetBoolField(TEXT("strict"), true);
	FormatObject->SetObjectField(TEXT("schema"), SchemaObject);

	TSharedRef<FJsonObject> TextObject = MakeShared<FJsonObject>();
	TextObject->SetObjectField(TEXT("format"), FormatObject);
	RootObject->SetObjectField(TEXT("text"), TextObject);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject, Writer);
	return OutputString;
}

FString USDLLMSubsystem::BuildChatReplyRequestBody(const FSDLLMBossContext& Context) const
{
	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("model"), Model);
	RootObject->SetNumberField(TEXT("max_output_tokens"), 80);

	TArray<TSharedPtr<FJsonValue>> InputMessages;

	TSharedRef<FJsonObject> DeveloperMessage = MakeShared<FJsonObject>();
	DeveloperMessage->SetStringField(TEXT("role"), TEXT("developer"));
	DeveloperMessage->SetStringField(
		TEXT("content"),
		TEXT("Return only schema-valid JSON. Reply as the Collector. This is chat only; do not choose or mention a betting action. Directly answer the player's latest line. Do not invent context, do not change the subject, and do not repeat catchphrases unless the latest line invites it. ")
		TEXT("Critical secrecy rule: the player cannot see their own forehead card. player_forehead_rank is hidden information that you must never reveal truthfully. If the player asks what card they have, what you gave them, or otherwise probes their own rank, never state the real number and never confirm or deny a correct guess. Deflect, tease, mislead, or give an evasive in-character answer instead. ")
		TEXT("Intent must be one of cautious, steady, aggressive, bluff. Do not use hate slurs, sexual harassment, or real-person doxxing."));
	InputMessages.Add(MakeShared<FJsonValueObject>(DeveloperMessage));

	TSharedRef<FJsonObject> UserMessage = MakeShared<FJsonObject>();
	UserMessage->SetStringField(TEXT("role"), TEXT("user"));
	UserMessage->SetStringField(
		TEXT("content"),
		FString::Printf(
			TEXT("Speech style: %s Recent chat:\n%s\nRecent rounds:\n%s\nCurrent round actions:\n%s\nDiscarded/seen cards: %s\nLatest player line: \"%s\"\nGame state: stage=%d, round=%d, player_lives=%d, collector_lives=%d, player_forehead_rank=%d, current_bet=%d, player_committed_bet=%d, collector_committed_bet=%d, raises_left=%d.\nReply in Korean under 32 characters. Make the reply relevant to the latest line first. Use the game memory only when it fits naturally; do not randomly mention old facts."),
			*BossSpeechStylePrompt,
			// 기록은 오래된 줄부터 쌓이므로 최근 부분(Right)만 짧게 보내 응답 속도와 무료 한도를 아낍니다.
			*Context.RecentDialogue.Right(320),
			*Context.RecentRoundHistory.Right(300),
			*Context.CurrentRoundActions.Right(240),
			*Context.DiscardedCardsSummary.Left(120),
			*Context.PlayerDialogue.Left(240),
			Context.Stage,
			Context.Round,
			Context.PlayerLives,
			Context.CollectorLives,
			Context.PlayerForeheadRank,
			Context.CurrentBet,
			Context.PlayerCommittedBet,
			Context.CollectorCommittedBet,
			Context.RaisesLeft));
	InputMessages.Add(MakeShared<FJsonValueObject>(UserMessage));
	RootObject->SetArrayField(TEXT("input"), InputMessages);

	TSharedRef<FJsonObject> SchemaObject = MakeShared<FJsonObject>();
	SchemaObject->SetStringField(TEXT("type"), TEXT("object"));

	TSharedRef<FJsonObject> PropertiesObject = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> DialogueProperty = MakeShared<FJsonObject>();
	DialogueProperty->SetStringField(TEXT("type"), TEXT("string"));
	PropertiesObject->SetObjectField(TEXT("dialogue"), DialogueProperty);

	TSharedRef<FJsonObject> IntentProperty = MakeShared<FJsonObject>();
	IntentProperty->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> IntentValues;
	IntentValues.Add(MakeShared<FJsonValueString>(TEXT("cautious")));
	IntentValues.Add(MakeShared<FJsonValueString>(TEXT("steady")));
	IntentValues.Add(MakeShared<FJsonValueString>(TEXT("aggressive")));
	IntentValues.Add(MakeShared<FJsonValueString>(TEXT("bluff")));
	IntentProperty->SetArrayField(TEXT("enum"), IntentValues);
	PropertiesObject->SetObjectField(TEXT("intent"), IntentProperty);

	SchemaObject->SetObjectField(TEXT("properties"), PropertiesObject);
	TArray<TSharedPtr<FJsonValue>> RequiredFields;
	RequiredFields.Add(MakeShared<FJsonValueString>(TEXT("dialogue")));
	RequiredFields.Add(MakeShared<FJsonValueString>(TEXT("intent")));
	SchemaObject->SetArrayField(TEXT("required"), RequiredFields);
	SchemaObject->SetBoolField(TEXT("additionalProperties"), false);

	TSharedRef<FJsonObject> FormatObject = MakeShared<FJsonObject>();
	FormatObject->SetStringField(TEXT("type"), TEXT("json_schema"));
	FormatObject->SetStringField(TEXT("name"), TEXT("showdown_boss_chat_reply"));
	FormatObject->SetBoolField(TEXT("strict"), true);
	FormatObject->SetObjectField(TEXT("schema"), SchemaObject);

	TSharedRef<FJsonObject> TextObject = MakeShared<FJsonObject>();
	TextObject->SetObjectField(TEXT("format"), FormatObject);
	RootObject->SetObjectField(TEXT("text"), TextObject);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject, Writer);
	return OutputString;
}

FString USDLLMSubsystem::BuildResultReactionRequestBody(const FSDLLMBossContext& Context) const
{
	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("model"), Model);
	RootObject->SetNumberField(TEXT("max_output_tokens"), 80);

	TArray<TSharedPtr<FJsonValue>> InputMessages;

	TSharedRef<FJsonObject> DeveloperMessage = MakeShared<FJsonObject>();
	DeveloperMessage->SetStringField(TEXT("role"), TEXT("developer"));
	DeveloperMessage->SetStringField(
		TEXT("content"),
		TEXT("Return only schema-valid JSON. You are the Collector and a betting round just ended; the cards are revealed. React to the outcome in one short in-character Korean line based on round_outcome. ")
		TEXT("round_outcome=collector_won means you won: be smug, menacing, or coldly satisfied. round_outcome=collector_lost means you lost: sound reluctantly impressed or regretful, like \"아쉽군\" — never celebrate. round_outcome=draw means a tie: be coldly indifferent or unsettled. ")
		TEXT("Use the recent chat and round history so the line feels like a continuation, not a random shout. Do not choose or mention a betting action. Intent must be one of cautious, steady, aggressive, bluff. Do not use hate slurs, sexual harassment, or real-person doxxing."));
	InputMessages.Add(MakeShared<FJsonValueObject>(DeveloperMessage));

	TSharedRef<FJsonObject> UserMessage = MakeShared<FJsonObject>();
	UserMessage->SetStringField(TEXT("role"), TEXT("user"));
	UserMessage->SetStringField(
		TEXT("content"),
		FString::Printf(
			TEXT("Speech style: %s Round just ended. round_outcome=%s\nRecent chat:\n%s\nRecent rounds:\n%s\nThis round actions:\n%s\nGame state: stage=%d, round=%d, player_lives=%d, collector_lives=%d, player_forehead_rank=%d, collector_forehead_rank=%d.\nReply in Korean under 32 characters as a reaction to the result. Match the tone to round_outcome."),
			*BossSpeechStylePrompt,
			*Context.RoundOutcome,
			*Context.RecentDialogue.Right(240),
			*Context.RecentRoundHistory.Right(240),
			*Context.CurrentRoundActions.Right(200),
			Context.Stage,
			Context.Round,
			Context.PlayerLives,
			Context.CollectorLives,
			Context.PlayerForeheadRank,
			Context.CollectorForeheadRank));
	InputMessages.Add(MakeShared<FJsonValueObject>(UserMessage));
	RootObject->SetArrayField(TEXT("input"), InputMessages);

	TSharedRef<FJsonObject> SchemaObject = MakeShared<FJsonObject>();
	SchemaObject->SetStringField(TEXT("type"), TEXT("object"));

	TSharedRef<FJsonObject> PropertiesObject = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> DialogueProperty = MakeShared<FJsonObject>();
	DialogueProperty->SetStringField(TEXT("type"), TEXT("string"));
	PropertiesObject->SetObjectField(TEXT("dialogue"), DialogueProperty);

	TSharedRef<FJsonObject> IntentProperty = MakeShared<FJsonObject>();
	IntentProperty->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> IntentValues;
	IntentValues.Add(MakeShared<FJsonValueString>(TEXT("cautious")));
	IntentValues.Add(MakeShared<FJsonValueString>(TEXT("steady")));
	IntentValues.Add(MakeShared<FJsonValueString>(TEXT("aggressive")));
	IntentValues.Add(MakeShared<FJsonValueString>(TEXT("bluff")));
	IntentProperty->SetArrayField(TEXT("enum"), IntentValues);
	PropertiesObject->SetObjectField(TEXT("intent"), IntentProperty);

	SchemaObject->SetObjectField(TEXT("properties"), PropertiesObject);
	TArray<TSharedPtr<FJsonValue>> RequiredFields;
	RequiredFields.Add(MakeShared<FJsonValueString>(TEXT("dialogue")));
	RequiredFields.Add(MakeShared<FJsonValueString>(TEXT("intent")));
	SchemaObject->SetArrayField(TEXT("required"), RequiredFields);
	SchemaObject->SetBoolField(TEXT("additionalProperties"), false);

	TSharedRef<FJsonObject> FormatObject = MakeShared<FJsonObject>();
	FormatObject->SetStringField(TEXT("type"), TEXT("json_schema"));
	FormatObject->SetStringField(TEXT("name"), TEXT("showdown_boss_result_reaction"));
	FormatObject->SetBoolField(TEXT("strict"), true);
	FormatObject->SetObjectField(TEXT("schema"), SchemaObject);

	TSharedRef<FJsonObject> TextObject = MakeShared<FJsonObject>();
	TextObject->SetObjectField(TEXT("format"), FormatObject);
	RootObject->SetObjectField(TEXT("text"), TextObject);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject, Writer);
	return OutputString;
}

bool USDLLMSubsystem::ParseBossResponse(const FString& ResponseBody, FSDLLMBossResponse& OutResponse) const
{
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return false;
	}

	const FString OutputText = ExtractOutputText(RootObject);
	if (OutputText.IsEmpty())
	{
		return false;
	}

	TSharedPtr<FJsonObject> BossObject;
	TSharedRef<TJsonReader<>> BossReader = TJsonReaderFactory<>::Create(OutputText);
	if (!FJsonSerializer::Deserialize(BossReader, BossObject) || !BossObject.IsValid())
	{
		return false;
	}

	BossObject->TryGetStringField(TEXT("dialogue"), OutResponse.Dialogue);
	BossObject->TryGetStringField(TEXT("intent"), OutResponse.Intent);

	FString ActionText;
	BossObject->TryGetStringField(TEXT("action"), ActionText);
	ActionText = ActionText.ToLower();
	if (ActionText == TEXT("check"))
	{
		OutResponse.Decision.Action = EShowDownBetAction::Check;
	}
	else if (ActionText == TEXT("call"))
	{
		OutResponse.Decision.Action = EShowDownBetAction::Call;
	}
	else if (ActionText == TEXT("raise"))
	{
		OutResponse.Decision.Action = EShowDownBetAction::Raise;
	}
	else if (ActionText == TEXT("fold"))
	{
		OutResponse.Decision.Action = EShowDownBetAction::Fold;
	}
	else
	{
		return false;
	}

	double TargetBet = 0.0;
	BossObject->TryGetNumberField(TEXT("target_bet"), TargetBet);
	OutResponse.Decision.TargetBet = FMath::RoundToInt(TargetBet);

	ClampResponse(OutResponse);
	return !OutResponse.Dialogue.IsEmpty();
}

bool USDLLMSubsystem::ParseBossChatReply(const FString& ResponseBody, FString& OutDialogue, FString& OutIntent) const
{
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return false;
	}

	const FString OutputText = ExtractOutputText(RootObject);
	if (OutputText.IsEmpty())
	{
		return false;
	}

	TSharedPtr<FJsonObject> BossObject;
	TSharedRef<TJsonReader<>> BossReader = TJsonReaderFactory<>::Create(OutputText);
	if (!FJsonSerializer::Deserialize(BossReader, BossObject) || !BossObject.IsValid())
	{
		return false;
	}

	BossObject->TryGetStringField(TEXT("dialogue"), OutDialogue);
	BossObject->TryGetStringField(TEXT("intent"), OutIntent);
	OutDialogue = OutDialogue.Left(48);
	if (OutIntent.IsEmpty())
	{
		OutIntent = TEXT("steady");
	}

	return !OutDialogue.IsEmpty();
}

FString USDLLMSubsystem::ExtractOutputText(const TSharedPtr<FJsonObject>& RootObject) const
{
	FString OutputText;
	if (RootObject->TryGetStringField(TEXT("output_text"), OutputText) && !OutputText.IsEmpty())
	{
		return OutputText;
	}

	// Chat Completions(Gemini) 응답: choices[0].message.content
	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (RootObject->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num() > 0)
	{
		const TSharedPtr<FJsonObject> Choice = (*Choices)[0]->AsObject();
		const TSharedPtr<FJsonObject>* MessageObject = nullptr;
		if (Choice.IsValid() && Choice->TryGetObjectField(TEXT("message"), MessageObject)
			&& (*MessageObject)->TryGetStringField(TEXT("content"), OutputText))
		{
			// 일부 모델(Gemma 등)은 JSON 앞뒤에 ```json 같은 군더더기를 붙이므로 중괄호 구간만 남깁니다.
			OutputText = OutputText.TrimStartAndEnd();
			int32 FirstBrace = INDEX_NONE;
			int32 LastBrace = INDEX_NONE;
			if (OutputText.FindChar(TEXT('{'), FirstBrace) && OutputText.FindLastChar(TEXT('}'), LastBrace) && LastBrace > FirstBrace)
			{
				OutputText = OutputText.Mid(FirstBrace, LastBrace - FirstBrace + 1);
			}
			return OutputText;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* OutputArray = nullptr;
	if (!RootObject->TryGetArrayField(TEXT("output"), OutputArray))
	{
		return FString();
	}

	for (const TSharedPtr<FJsonValue>& OutputValue : *OutputArray)
	{
		const TSharedPtr<FJsonObject> OutputObject = OutputValue->AsObject();
		if (!OutputObject.IsValid())
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
		if (!OutputObject->TryGetArrayField(TEXT("content"), ContentArray))
		{
			continue;
		}

		for (const TSharedPtr<FJsonValue>& ContentValue : *ContentArray)
		{
			const TSharedPtr<FJsonObject> ContentObject = ContentValue->AsObject();
			if (ContentObject.IsValid() && ContentObject->TryGetStringField(TEXT("text"), OutputText))
			{
				return OutputText;
			}
		}
	}

	return FString();
}

void USDLLMSubsystem::ClampResponse(FSDLLMBossResponse& Response) const
{
	Response.Dialogue = Response.Dialogue.Left(48);
	Response.Decision.TargetBet = FMath::Clamp(Response.Decision.TargetBet, 0, 6);
}
