#include "ShowDownLoginWidget.h"

#include "SupabaseSubsystem.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "ShowDownMainMenuWidget.h"
#include "Blueprint/WidgetTree.h"

void UShowDownLoginWidget::SetUseLegacyNavigation(bool bInUseLegacyNavigation)
{
	bUseLegacyNavigation = bInUseLegacyNavigation;
}

void UShowDownLoginWidget::FocusIdInput()
{
	if (!EditableTextBox_Id)
	{
		return;
	}

	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		EditableTextBox_Id->SetUserFocus(PlayerController);
	}
	EditableTextBox_Id->SetKeyboardFocus();
}

void UShowDownLoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// WBP_Login의 Button_Login 위젯이 정상적으로 연결되어 있으면
	// 버튼 클릭 이벤트를 C++ 함수 HandleLoginClicked에 연결합니다.
	if (Button_Login)
	{
		Button_Login->OnClicked.AddUniqueDynamic(this, &UShowDownLoginWidget::HandleLoginClicked);
	}
	if (EditableTextBox_Id)
	{
		EditableTextBox_Id->OnTextCommitted.AddUniqueDynamic(
			this,
			&UShowDownLoginWidget::HandleCredentialTextCommitted);
	}
	if (EditableTextBox_Password)
	{
		EditableTextBox_Password->OnTextCommitted.AddUniqueDynamic(
			this,
			&UShowDownLoginWidget::HandleCredentialTextCommitted);
	}

	// GameInstance에 등록된 SupabaseSubsystem을 가져옵니다.
	// 로그인 요청 결과를 UI가 받을 수 있도록 OnLoginResult 이벤트에 함수를 연결합니다.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USupabaseSubsystem* SupabaseSubsystem = GameInstance->GetSubsystem<USupabaseSubsystem>())
		{
			SupabaseSubsystem->OnLoginResult.AddUniqueDynamic(this, &UShowDownLoginWidget::HandleLoginResult);
		}
	}

	HideStatusMessage();
	// 시연 중 네트워크가 막혀도 바로 진행할 수 있게 오프라인 시작 버튼은 항상 보여줍니다.
	EnsureOfflineButton();
	SetOfflineButtonVisible(true);
}

void UShowDownLoginWidget::EnsureOfflineButton()
{
	if (Button_Offline || !WidgetTree || !Text_Status)
	{
		return;
	}

	UPanelWidget* StatusParent = Text_Status->GetParent();
	if (!StatusParent)
	{
		return;
	}

	Button_Offline = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Button_Offline"));
	Button_Offline->SetBackgroundColor(FLinearColor(0.55f, 0.38f, 0.08f, 1.0f));

	UTextBlock* OfflineLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_OfflineLabel"));
	OfflineLabel->SetText(FText::FromString(TEXT("오프라인으로 시작 (싱글플레이)")));
	OfflineLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	OfflineLabel->SetJustification(ETextJustify::Center);
	FSlateFontInfo OfflineFont = OfflineLabel->GetFont();
	OfflineFont.Size = 15;
	OfflineLabel->SetFont(OfflineFont);
	Button_Offline->AddChild(OfflineLabel);

	// WBP_Login은 캔버스 배치라서 LOGIN 버튼과 같은 기준점에 두고 바로 오른쪽으로 옮깁니다.
	UCanvasPanelSlot* LoginCanvasSlot = Button_Login ? Cast<UCanvasPanelSlot>(Button_Login->Slot) : nullptr;
	UCanvasPanel* LoginCanvas = LoginCanvasSlot ? Cast<UCanvasPanel>(Button_Login->GetParent()) : nullptr;
	if (LoginCanvas)
	{
		UCanvasPanelSlot* OfflineCanvasSlot = LoginCanvas->AddChildToCanvas(Button_Offline);
		OfflineCanvasSlot->SetAnchors(LoginCanvasSlot->GetAnchors());
		OfflineCanvasSlot->SetAlignment(LoginCanvasSlot->GetAlignment());
		const FVector2D LoginSize = LoginCanvasSlot->GetSize();
		OfflineCanvasSlot->SetPosition(LoginCanvasSlot->GetPosition() + FVector2D(LoginSize.X + 12.0f, 0.0f));
		OfflineCanvasSlot->SetSize(FVector2D(FMath::Max(LoginSize.X, 260.0f), LoginSize.Y));
		OfflineCanvasSlot->SetZOrder(LoginCanvasSlot->GetZOrder() + 1);
	}
	else
	{
		UPanelSlot* OfflineSlot = StatusParent->AddChild(Button_Offline);
		if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(OfflineSlot))
		{
			VerticalSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
			VerticalSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}

	Button_Offline->OnClicked.AddUniqueDynamic(this, &UShowDownLoginWidget::HandleOfflineClicked);
}

void UShowDownLoginWidget::SetOfflineButtonVisible(const bool bVisible)
{
	if (Button_Offline)
	{
		Button_Offline->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UShowDownLoginWidget::HandleOfflineClicked()
{
	USupabaseSubsystem* SupabaseSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<USupabaseSubsystem>()
		: nullptr;
	if (!SupabaseSubsystem)
	{
		return;
	}

	SupabaseSubsystem->StartOfflineGuestSession();

	if (Button_Login)
	{
		Button_Login->SetIsEnabled(true);
	}

	// 정상 로그인 성공과 같은 경로로 메인 메뉴에 들어갑니다.
	HandleLoginResult(true, TEXT("Offline guest session."));
}

void UShowDownLoginWidget::BuildFigmaLayout()
{
	if (!WidgetTree)
	{
		return;
	}

	const FLinearColor Ink(0.92f, 0.95f, 0.96f, 1.0f);
	const FLinearColor MutedInk(0.70f, 0.75f, 0.77f, 1.0f);
	const FLinearColor PanelColor(0.015f, 0.025f, 0.03f, 0.78f);
	const FLinearColor FieldColor(0.08f, 0.10f, 0.11f, 0.92f);
	const FLinearColor Accent(0.12f, 0.68f, 0.78f, 1.0f);

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("FigmaLoginRoot"));
	WidgetTree->RootWidget = Root;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LoginGlassPanel"));
	Panel->SetBrushColor(PanelColor);
	Panel->SetPadding(FMargin(34.0f, 30.0f));
	UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
	PanelSlot->SetAnchors(FAnchors(0.18f, 0.50f));
	PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	PanelSlot->SetSize(FVector2D(500.0f, 430.0f));

	UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LoginStack"));
	Panel->SetContent(Stack);

	UBorder* BrandPlate = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BrandPlate"));
	BrandPlate->SetBrushColor(FLinearColor(0.01f, 0.02f, 0.025f, 0.55f));
	UTextBlock* Brand = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Brand"));
	Brand->SetText(FText::FromString(TEXT("SHOWDOWN")));
	Brand->SetColorAndOpacity(FSlateColor(Ink));
	Brand->SetJustification(ETextJustify::Center);
	FSlateFontInfo BrandFont = Brand->GetFont();
	BrandFont.Size = 38;
	Brand->SetFont(BrandFont);
	BrandPlate->SetContent(Brand);
	UVerticalBoxSlot* BrandSlot = Stack->AddChildToVerticalBox(BrandPlate);
	BrandSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 28.0f));
	BrandSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	auto AddInputRow = [&](const TCHAR* LabelText, const TCHAR* WidgetName, bool bPassword) -> UEditableTextBox*
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(LabelText));
		Label->SetColorAndOpacity(FSlateColor(MutedInk));
		FSlateFontInfo LabelFont = Label->GetFont();
		LabelFont.Size = 17;
		Label->SetFont(LabelFont);
		UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label);
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetHorizontalAlignment(HAlign_Right);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 14.0f, 0.0f));

		UBorder* FieldBackground = WidgetTree->ConstructWidget<UBorder>();
		FieldBackground->SetBrushColor(FieldColor);
		FieldBackground->SetPadding(FMargin(10.0f, 4.0f));
		UEditableTextBox* Field = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), WidgetName);
		Field->SetHintText(FText::FromString(bPassword ? TEXT("Password") : TEXT("ID")));
		Field->SetIsPassword(bPassword);
		Field->SetForegroundColor(Ink);
		Field->SetMinDesiredWidth(300.0f);
		FieldBackground->SetContent(Field);
		UHorizontalBoxSlot* FieldSlot = Row->AddChildToHorizontalBox(FieldBackground);
		FieldSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		FieldSlot->SetVerticalAlignment(VAlign_Fill);

		UVerticalBoxSlot* RowSlot = Stack->AddChildToVerticalBox(Row);
		RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
		RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		return Field;
	};

	EditableTextBox_Id = AddInputRow(TEXT("ID"), TEXT("EditableTextBox_Id"), false);
	EditableTextBox_Password = AddInputRow(TEXT("Password"), TEXT("EditableTextBox_Password"), true);

	Button_Login = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Button_Login"));
	Button_Login->SetBackgroundColor(Accent);
	UTextBlock* LoginLabel = WidgetTree->ConstructWidget<UTextBlock>();
	LoginLabel->SetText(FText::FromString(TEXT("LOGIN")));
	LoginLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	LoginLabel->SetJustification(ETextJustify::Center);
	FSlateFontInfo LoginFont = LoginLabel->GetFont();
	LoginFont.Size = 16;
	LoginLabel->SetFont(LoginFont);
	Button_Login->AddChild(LoginLabel);
	UVerticalBoxSlot* ButtonSlot = Stack->AddChildToVerticalBox(Button_Login);
	ButtonSlot->SetPadding(FMargin(130.0f, 8.0f, 0.0f, 0.0f));
	ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
	ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));

	Text_Status = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Status"));
	Text_Status->SetColorAndOpacity(FSlateColor(MutedInk));
	Text_Status->SetJustification(ETextJustify::Center);
	// 네트워크 진단 안내처럼 긴 문구가 패널 밖으로 잘리지 않게 줄바꿈합니다.
	Text_Status->SetAutoWrapText(true);
	UVerticalBoxSlot* StatusSlot = Stack->AddChildToVerticalBox(Text_Status);
	StatusSlot->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 0.0f));
	StatusSlot->SetHorizontalAlignment(HAlign_Fill);
}

void UShowDownLoginWidget::NativeDestruct()
{
	if (Button_Login)
	{
		Button_Login->OnClicked.RemoveDynamic(this, &UShowDownLoginWidget::HandleLoginClicked);
	}
	if (EditableTextBox_Id)
	{
		EditableTextBox_Id->OnTextCommitted.RemoveDynamic(
			this,
			&UShowDownLoginWidget::HandleCredentialTextCommitted);
	}
	if (EditableTextBox_Password)
	{
		EditableTextBox_Password->OnTextCommitted.RemoveDynamic(
			this,
			&UShowDownLoginWidget::HandleCredentialTextCommitted);
	}

	// 위젯이 제거될 때 SupabaseSubsystem에 연결했던 이벤트를 해제합니다.
	// 이걸 하지 않으면 위젯이 사라진 뒤에도 이벤트가 호출될 수 있습니다.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USupabaseSubsystem* SupabaseSubsystem = GameInstance->GetSubsystem<USupabaseSubsystem>())
		{
			SupabaseSubsystem->OnLoginResult.RemoveDynamic(this, &UShowDownLoginWidget::HandleLoginResult);
		}
	}

	Super::NativeDestruct();
}

void UShowDownLoginWidget::HandleLoginClicked()
{
	if (bLoginRequestInFlight)
	{
		return;
	}

	// 입력창에서 사용자가 입력한 "아이디"를 가져옵니다.
	// 연결되어 있지 않으면 빈 문자열을 사용해서 크래시를 막습니다.
	const FString LoginId = EditableTextBox_Id
		? EditableTextBox_Id->GetText().ToString()
		: TEXT("");

	// 비밀번호 입력창이 정상적으로 연결되어 있으면 사용자가 입력한 비밀번호를 가져옵니다.
	// Supabase 로그인 요청에 이 값이 사용됩니다.
	const FString Password = EditableTextBox_Password
		? EditableTextBox_Password->GetText().ToString()
		: TEXT("");

	if (LoginId.TrimStartAndEnd().IsEmpty() || Password.IsEmpty())
	{
		ShowStatusError(TEXT("Enter your ID and password."));
		return;
	}

	// 버튼을 누른 직후에는 서버 응답을 기다리는 중이라는 메시지를 보여줍니다.
	bLoginRequestInFlight = true;
	if (Button_Login)
	{
		Button_Login->SetIsEnabled(false);
	}

	// A new valid attempt clears any previous failure. The disabled login button
	// already communicates progress, so the status row stays reserved for errors.
	HideStatusMessage();

	// SupabaseSubsystem을 가져와서 실제 로그인 요청을 보냅니다.
	// HTTP 요청과 토큰 처리는 SupabaseSubsystem 쪽에서 담당합니다.
	USupabaseSubsystem* SupabaseSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<USupabaseSubsystem>()
		: nullptr;
	if (SupabaseSubsystem)
	{
		SupabaseSubsystem->LoginWithId(LoginId, Password);
	}
	else
	{
		// Subsystem을 찾지 못하면 로그인 요청을 보낼 수 없으므로 UI에 오류를 표시합니다.
		bLoginRequestInFlight = false;
		if (Button_Login)
		{
			Button_Login->SetIsEnabled(true);
		}
		ShowStatusError(TEXT("Supabase subsystem not found"));
	}
}

void UShowDownLoginWidget::HandleCredentialTextCommitted(
	const FText& Text,
	ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		HandleLoginClicked();
	}
}

void UShowDownLoginWidget::HandleLoginResult(bool bSuccess, const FString& Message)
{
	const bool bIsProgressMessage = Message == TEXT("Logging in...");

	if (bIsProgressMessage)
	{
		HideStatusMessage();
		return;
	}

	// 네트워크 재시도 중에는 안내만 보여주고 버튼은 잠근 채로 둡니다.
	if (Message.StartsWith(TEXT("Connection unstable.")))
	{
		ShowStatusError(Message);
		return;
	}

	if (!bSuccess)
	{
		ShowStatusError(Message);
		bLoginRequestInFlight = false;
		if (Button_Login)
		{
			Button_Login->SetIsEnabled(true);
		}
		return;
	}

	HideStatusMessage();
	bLoginRequestInFlight = false;
	OnLoginSucceeded.Broadcast();

	// HubFlowManager가 화면 전환을 담당하는 경우 여기서 멈춥니다.
	// 기존 L_MainMenu에서 LoginWidget만 직접 띄우는 흐름은 아래 레거시 경로가 계속 지원합니다.
	if (!bUseLegacyNavigation)
	{
		return;
	}

	// 로그인에 성공했더라도, 어떤 메인 메뉴 WBP를 띄울지 지정되어 있지 않으면 생성할 수 없습니다.
	// WBP_Login의 Class Defaults에서 MainMenuWidgetClass에 WBP_MainMenu를 지정해야 합니다.
	if (!MainMenuWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("MainMenuWidgetClass is not set."));
		return;
	}

	// 현재 로그인 위젯을 소유한 PlayerController를 가져옵니다.
	// 새 위젯을 만들 때 Owning Player로 넘기고, 마우스/UI 입력 모드도 이 컨트롤러에 설정합니다.
	APlayerController* PlayerController = GetOwningPlayer();

	// 지정해둔 WBP_MainMenu 클래스를 기반으로 실제 메인 메뉴 위젯 인스턴스를 생성합니다.
	// C++ 타입은 UShowDownMainMenuWidget이고, 실제 디자인은 WBP_MainMenu가 담당합니다.
	MainMenuWidget = CreateWidget<UShowDownMainMenuWidget>(
		PlayerController,
		MainMenuWidgetClass
	);

	// 위젯 생성에 실패하면 이후 AddToViewport를 할 수 없으므로 로그를 남기고 종료합니다.
	if (!MainMenuWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to create main menu widget."));
		return;
	}

	// 로그인 화면은 더 이상 필요 없으므로 화면에서 제거합니다.
	// 이 다음부터는 메인 메뉴 화면만 보이게 됩니다.
	RemoveFromParent();

	// 생성한 메인 메뉴 위젯을 실제 화면에 표시합니다.
	MainMenuWidget->AddToViewport();

	// UI를 마우스로 조작할 수 있도록 PlayerController 설정을 바꿉니다.
	if (PlayerController)
	{
		// 마우스 커서를 화면에 보이게 합니다.
		PlayerController->bShowMouseCursor = true;

		// 키보드/마우스 입력을 게임 캐릭터 조작이 아니라 UI에 집중시키는 모드로 바꿉니다.
		FInputModeUIOnly InputMode;

		// 방금 띄운 메인 메뉴 위젯에 포커스를 줍니다.
		// 버튼이나 입력창을 바로 조작할 수 있게 하기 위한 설정입니다.
		// InputMode.SetWidgetToFocus(MainMenuWidget->TakeWidget());

		// PlayerController에 UI Only 입력 모드를 적용합니다.
		PlayerController->SetInputMode(InputMode);
	}
}

void UShowDownLoginWidget::HideStatusMessage()
{
	if (!Text_Status)
	{
		return;
	}

	Text_Status->SetText(FText::GetEmpty());
	Text_Status->SetVisibility(ESlateVisibility::Collapsed);
}

void UShowDownLoginWidget::ShowStatusError(const FString& Message)
{
	if (!Text_Status)
	{
		return;
	}

	Text_Status->SetText(FText::FromString(Message));
	Text_Status->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
	Text_Status->SetVisibility(ESlateVisibility::HitTestInvisible);
}
