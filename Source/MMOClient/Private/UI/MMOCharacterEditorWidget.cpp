// Copyright MMOClient Project. All Rights Reserved.

#include "UI/MMOCharacterEditorWidget.h"
#include "Character/MMOCharacterEditorSubsystem.h"
#include "Net/MMOConnectionManager.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Game/MMOAssetLibrary.h"

void UMMOCharacterEditorWidget::NativeConstruct()
{
	Super::NativeConstruct();

	CreateBtn->OnClicked.AddDynamic(this, &UMMOCharacterEditorWidget::OnCreateClicked);
	RandomizeBtn->OnClicked.AddDynamic(this, &UMMOCharacterEditorWidget::OnRandomize);
	TemplateCombo->OnSelectionChanged.AddDynamic(this, &UMMOCharacterEditorWidget::OnTemplateChosen);

	SetStatus(TEXT("Ожидание данных с сервера…"), false);
}

void UMMOCharacterEditorWidget::InitFromConnection(UMMOConnectionManager* Conn)
{
	if (!Conn)
	{
		SetStatus(TEXT("Нет соединения с миром."), true);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	EditorSub = GI ? GI->GetSubsystem<UMMOCharacterEditorSubsystem>() : nullptr;
	if (!EditorSub)
	{
		SetStatus(TEXT("Редактор недоступен: subsystem не найден."), true);
		return;
	}

	EditorSub->BindConnection(Conn);
	EditorSub->OnCharTemplatesReady.AddDynamic(this, &UMMOCharacterEditorWidget::HandleTemplatesReady);
	EditorSub->OnCharCreateResult.AddDynamic(this, &UMMOCharacterEditorWidget::HandleCreateResult);

	FString Err;
	if (!EditorSub->RequestRosterAndTemplates(Err))
	{
		SetStatus(FString::Printf(TEXT("Запрос шаблонов не удался: %s"), *Err), true);
	}
}

void UMMOCharacterEditorWidget::HandleTemplatesReady()
{
	const TArray<FMMOCharTemplate>& Tpls = EditorSub->GetTemplates();
	TemplateCombo->ClearOptions();
	for (const FMMOCharTemplate& T : Tpls)
	{
		TemplateCombo->AddOption(MakeTemplateLabel(T));
	}
	if (Tpls.Num() > 0)
	{
		TemplateCombo->SelectOption(MakeTemplateLabel(Tpls[0]), ESelectInfo::Direct);
		OnTemplateChosen(MakeTemplateLabel(Tpls[0]), ESelectInfo::Direct);
	}
	SetStatus(FString::Printf(TEXT("Шаблонов: %d. Выберите образ и имя."), Tpls.Num()), false);
}

void UMMOCharacterEditorWidget::OnTemplateChosen(FString Selection, ESelectInfo::Type /*SelectInfo*/)
{
	const FMMOCharTemplate* T = FindTemplateByLabel(Selection);
	CurrentTemplateKey = T ? T->TemplateKey : FString();
	RebuildSlotCombos();
	UpdatePreviewFromSlots();
}

void UMMOCharacterEditorWidget::RebuildSlotCombos()
{
	SlotsPanel->ClearChildren();
	SlotCombos.Reset();

	const FMMOCharTemplate* T = FindTemplateByLabel(TemplateCombo->GetSelectedOption());
	if (!T) return;

	for (const auto& Pair : T->AppearanceSlots)
	{
		const FString SlotKey = Pair.Key;
		const TArray<FString>& Values = Pair.Value;

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
			*FString::Printf(TEXT("SlotRow_%s"), *SlotKey));
		SlotsPanel->AddChild(Row);

		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(FText::FromString(SlotKey));
		Row->AddChildToHorizontalBox(Label)->SetWidthInSlot(140.f);

		UComboBoxString* Combo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(),
			*FString::Printf(TEXT("SlotCombo_%s"), *SlotKey));
		for (const FString& V : Values) Combo->AddOption(V);
		if (Values.Num() > 0) Combo->SelectOption(Values[0], ESelectInfo::Direct);
		// UComboBoxString::OnSelectionChanged — general-делегат (FOnComboBoxStringSelection):
		// подписка без AddDynamic; slot определяем по имени комбобокса в обработчике.
		Combo->OnSelectionChanged.AddUObject(this, &UMMOCharacterEditorWidget::HandleAnySlotChanged);
		Row->AddChildToHorizontalBox(Combo)->SetHAlign(HAlign_Fill)->SetFillWidth(1.f);

		SlotCombos.Add(SlotKey, Combo);
	}
}

void UMMOCharacterEditorWidget::HandleAnySlotChanged(FString Selection, ESelectInfo::Type /*SelectInfo*/)
{
	// Слот определяем по имени комбобокса: WidgetTree даёт объект по FName (SlotCombo_<key>).
	// Перебираем известные слоты и проверяем выбранное значение напрямую.
	for (const auto& Pair : SlotCombos)
	{
		UComboBoxString* Combo = WidgetTree->FindWidget<FName>(*FString::Printf(TEXT("SlotCombo_%s"), *Pair.Key), true);
		if (Combo && Combo == Pair.Value.Get() && Combo->GetSelectedOption() == Selection)
		{
			if (Pair.Key == TEXT("body_preset"))
			{
				UpdatePreviewFromSlots();
			}
			return;
		}
	}
}

void UMMOCharacterEditorWidget::UpdatePreviewFromSlots()
{
	if (!PreviewImage) return;
	const TObjectPtr<UComboBoxString>* BodyCombo = SlotCombos.Find(TEXT("body_preset"));
	if (!BodyCombo || !BodyCombo->Get()) return;
	const FString Preset = (*BodyCombo)->GetSelectedOption();

	// 1) Реальный арт художника (заполненный BodyPresetSprites в WBP) — приоритет.
	UTexture2D* Tex = nullptr;
	if (TObjectPtr<UTexture2D>* Found = BodyPresetSprites.Find(Preset))
	{
		Tex = *Found;
	}
	// 2) Fallback до появления арта: процедурный силуэт по ключу пресета,
	//    оттенок детерминирован по расе выбранного шаблона.
	if (!Tex && EditorSub && !CurrentTemplateKey.IsEmpty())
	{
		FString RaceKey(TEXT("default"));
		for (const FMMOCharTemplate& T : EditorSub->GetTemplates())
		{
			if (T.TemplateKey == CurrentTemplateKey)
			{
				RaceKey = T.RaceKey;
				break;
			}
		}
		Tex = UMMOAssetLibrary::MakeBodyPresetSprite(Preset,
			UMMOAssetLibrary::ColorFromStringKey(RaceKey, 0.45f, 0.9f));
	}
	if (Tex)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(Tex);
		Brush.ImageSize = FVector2D(256.f, 256.f);
		PreviewImage->SetBrush(Brush);
	}
}

void UMMOCharacterEditorWidget::ApplyPreset(const FString& PresetName)
{
	// Пресеты из DB описывают набор значений слотов; здесь — минимальная семантика:
	// body_preset всегда присутствует в схемах шаблонов, крутим его + первые значения остальных слотов.
	if (TObjectPtr<UComboBoxString>* BodyCombo = SlotCombos.Find(TEXT("body_preset")))
	{
		if ((*BodyCombo) && (*BodyCombo)->OptionExists(PresetName))
		{
			(*BodyCombo)->SelectOption(PresetName, ESelectInfo::Direct);
		}
	}
	UpdatePreviewFromSlots();
}

void UMMOCharacterEditorWidget::OnRandomize()
{
	const FMMOCharTemplate* T = FindTemplateByLabel(TemplateCombo->GetSelectedOption());
	if (!T) return;
	if (T->Presets.Num() > 0)
	{
		const int32 Idx = FMath::RandRange(0, T->Presets.Num() - 1);
		ApplyPreset(T->Presets[Idx]);
	}
	else
	{
		for (auto& Pair : SlotCombos)
		{
			if (Pair.Value && Pair.Value->NumOptions() > 0)
			{
				Pair.Value->SelectOption(Pair.Value->GetOption(FMath::RandRange(0, Pair.Value->NumOptions() - 1)),
				                         ESelectInfo::Direct);
			}
		}
		UpdatePreviewFromSlots();
	}
}

void UMMOCharacterEditorWidget::OnCreateClicked()
{
	FString Err;
	const TMap<FString, FString> Appearance = CollectAppearance();
	if (!EditorSub->CreateCharacter(CurrentTemplateKey, NameField->GetText().ToString(), Appearance, Err))
	{
		SetStatus(Err.IsEmpty() ? TEXT("Создание персонажа отклонено.") : Err, true);
	}
	else
	{
		SetStatus(TEXT("Отправка на сервер…"), false);
	}
}

void UMMOCharacterEditorWidget::HandleCreateResult(bool bSuccess, const FString& Message)
{
	if (bSuccess)
	{
		SetStatus(TEXT("Персонаж создан. Вход в мир…"), false);
		FString Err;
		EditorSub->EnterWorld(EditorSub->GetLastCreatedCharacterId(), Err); // WorldReady -> спавн карты
	}
	else
	{
		SetStatus(Message, true); // серверный CHAR_* код уже человекочитаем в subsystem
	}
}

TMap<FString, FString> UMMOCharacterEditorWidget::CollectAppearance() const
{
	TMap<FString, FString> Out;
	for (const auto& Pair : SlotCombos)
	{
		if (Pair.Value)
		{
			Out.Add(Pair.Key, Pair.Value->GetSelectedOption());
		}
	}
	return Out;
}

const FMMOCharTemplate* UMMOCharacterEditorWidget::FindTemplateByLabel(const FString& Label) const
{
	if (!EditorSub) return nullptr;
	for (const FMMOCharTemplate& T : EditorSub->GetTemplates())
	{
		if (MakeTemplateLabel(T) == Label) return &T;
	}
	return nullptr;
}

FString UMMOCharacterEditorWidget::MakeTemplateLabel(const FMMOCharTemplate& T) const
{
	return FString::Printf(TEXT("%s (%s / %s)"), *T.DisplayName, *T.RaceKey, *T.ClassKey);
}

void UMMOCharacterEditorWidget::SetStatus(const FString& Text, bool bError)
{
	StatusText->SetText(FText::FromString(Text));
	StatusText->SetColorAndOpacity(bError ? FSlateColor(FLinearColor::Red) : FSlateColor(FLinearColor::White));
}
