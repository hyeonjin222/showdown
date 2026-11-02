#pragma once

#include "CoreMinimal.h"
#include "ShowDownUserWidget.h"
#include "Templates/SubclassOf.h"
#include "Types/SlateEnums.h"
#include "ShowDownLoginWidget.generated.h"

class UEditableTextBox;
class UButton;
class UTextBlock;
class UShowDownMainMenuWidget;

// 로그인 성공 후 화면 전환이 필요하다는 요청입니다.
// HubFlowManager가 이 이벤트를 받아 카메라/페이드/UI 전환을 한 곳에서 처리할 수 있습니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnShowDownLoginSucceeded);

UCLASS()
class SHOWDOWN_API UShowDownLoginWidget : public UShowDownUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "ShowDown|Flow")
	FOnShowDownLoginSucceeded OnLoginSucceeded;

	// true면 기존처럼 LoginWidget이 직접 MainMenu를 생성합니다.
	// FlowManager가 화면 전환을 맡을 때는 false로 꺼서 UI와 연출 흐름을 분리합니다.
	void SetUseLegacyNavigation(bool bInUseLegacyNavigation);

	/** Moves keyboard input directly into the ID field. */
	void FocusIdInput();

protected:
	// 위젯이 화면에 생성되고 사용할 준비가 되었을 때 호출됩니다.
	// 여기서 버튼 클릭 이벤트와 Supabase 로그인 결과 이벤트를 연결합니다.
	virtual void NativeConstruct() override;

	// 위젯이 화면에서 사라지거나 제거될 때 호출됩니다.
	// 이벤트 연결을 해제해서 중복 호출이나 잘못된 접근을 막습니다.
	virtual void NativeDestruct() override;

	// Rebuilds the presentation layer at runtime while preserving the existing
	// Blueprint class and all C++ login bindings.
	void BuildFigmaLayout();

private:
	bool bUseLegacyNavigation = true;
	bool bLoginRequestInFlight = false;

	// WBP_Login 안에 있는 아이디 입력창과 연결됩니다.
	// WBP에서 위젯 이름이 EditableTextBox_Id와 정확히 같아야 합니다.
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* EditableTextBox_Id;

	// WBP_Login 안에 있는 비밀번호 입력창과 연결됩니다.
	// 사용자가 입력한 비밀번호를 C++에서 읽기 위해 사용합니다.
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* EditableTextBox_Password;

	// WBP_Login 안에 있는 로그인 버튼과 연결됩니다.
	// 이 버튼이 눌리면 HandleLoginClicked 함수가 실행됩니다.
	UPROPERTY(meta = (BindWidget))
	UButton* Button_Login;

	// WBP_Login 안에 있는 상태 메시지 텍스트와 연결됩니다.
	// 로그인 대기, 로그인 중, 성공, 실패 같은 문구를 표시합니다.
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Status;

	// 로그인 버튼을 눌렀을 때 실행되는 함수입니다.
	// 이메일과 비밀번호를 입력창에서 읽고 SupabaseSubsystem에 로그인 요청을 보냅니다.
	UFUNCTION()
	void HandleLoginClicked();

	UFUNCTION()
	void HandleCredentialTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	// SupabaseSubsystem에서 로그인 결과를 알려주면 실행되는 함수입니다.
	// 성공/실패 메시지를 받아서 Text_Status에 표시합니다.
	UFUNCTION()
	void HandleLoginResult(bool bSuccess, const FString& Message);

	void HideStatusMessage();
	void ShowStatusError(const FString& Message);

	// 로그인 화면에 항상 보이는 "오프라인으로 시작" 버튼입니다. 서버 없이 싱글플레이 시연을 시작합니다.
	// WBP_Login 에셋을 고치지 않도록 실행 중에 상태 문구 아래에 붙입니다.
	UPROPERTY(Transient)
	UButton* Button_Offline = nullptr;

	void EnsureOfflineButton();
	void SetOfflineButtonVisible(bool bVisible);

	UFUNCTION()
	void HandleOfflineClicked();
	
	// 로그인 성공 후 띄울 메인 메뉴 WBP 클래스입니다.
	// 에디터에서 WBP_MainMenu를 지정합니다.
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UShowDownMainMenuWidget> MainMenuWidgetClass;

	// 생성된 메인 메뉴 위젯을 저장합니다.
	UPROPERTY()
	UShowDownMainMenuWidget* MainMenuWidget;
};
