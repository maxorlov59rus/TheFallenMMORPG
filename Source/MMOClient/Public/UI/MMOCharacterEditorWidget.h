// Copyright MMOClient Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "MMOCharacterEditorWidget.generated.h"

class UMMOCharacterEditorSubsystem;
class UMMOConnectionManager;
class UTextBlock;
class UEditableTextBox;
class UButton;
class UVerticalBox;
class UImage;
class UTexture2D;

/**
 * Экран редактора персонажа (CHARACTER_FLOW п.2: шаблон -> имя -> слоты внешности -> создание).
 *
 * Никаких захардкоженных списков — всё рисуется из данных S2C CharTemplates (0x3003),
 * источник истины — БД ogni_characters.char_template:
 *   - TemplateCombo    <- GetTemplates() (метки "DisplayName (Раса / Класс)");
 *   - SlotsPanel       <- динамические ComboBoxString по одному на ключ AppearanceSlots,
 *                         опции = allowed_values выбранного шаблона;
 *   - RandomizeBtn     <- Presets шаблона (в т.ч. body_preset) + случайные значения слотов;
 *   - PreviewImage     <- переключение спрайта по значению слота body_preset (BodyPresetSprites);
 *   - CreateBtn        <- локальная предвалидация имени + CreateCharacter();
 *                         серверные ошибки CHAR_* показываются в StatusText.
 *
 * В WBP_CharacterEditor создать виджеты с указанными именами (BindWidget подхватит их).
 */
UCLASS(Abstract)
class MMOCLIENT_API UMMOCharacterEditorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	/** Вызывается родителем после ConnectedToWorld: подписка на subsystem + первый запрос ростера/шаблонов. */
	UFUNCTION(BlueprintCallable, Category = "MMO|Char")
	void InitFromConnection(UMMOConnectionManager* Conn);

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> TemplateCombo;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> NameField;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RandomizeBtn;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CreateBtn;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatusText;

	/** контейнер для динамических ComboBox'ов слотов внешности */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> SlotsPanel;

	/** превью тела по body_preset (опционально) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> PreviewImage;

	/** спрайты под body_preset-значения из DB ('default','hound_of_odin',...).
	 *  Если ключа нет — превью не меняется (отсутствие арта не блокирует UI). */
	UPROPERTY(EditAnywhere, Category = "MMO|Char|Preview", meta = (DisplayName = "Body Preset Sprites"))
	TMap<FString, TObjectPtr<UTexture2D>> BodyPresetSprites;

private:
	UPROPERTY()
	TObjectPtr<UMMOCharacterEditorSubsystem> EditorSub;

	/** slot-key -> динамически созданный комбобокс */
	TMap<FString, TObjectPtr<UComboBoxString>> SlotCombos;

	FString CurrentTemplateKey;

	UFUNCTION() void OnTemplateChosen(FString Selection, ESelectInfo::Type SelectInfo);
	UFUNCTION() void OnRandomize();
	UFUNCTION() void OnCreateClicked();

	UFUNCTION() void HandleTemplatesReady();

	/** серверный ответ на CharCreate: bSuccess + человекочитаемое сообщение/имя.
	 *  ID созданного персонажа доступен через GetLastCreatedCharacterId(). */
	UFUNCTION() void HandleCreateResult(bool bSuccess, const FString& Message);

	/** general-делегат UComboBoxString: определяет slot по имени комбобокса (SlotCombo_<key>) */
	void HandleAnySlotChanged(FString Selection, ESelectInfo::Type SelectInfo);

	TMap<FString, FString> CollectAppearance() const;
	void RebuildSlotCombos();
	void ApplyPreset(const FString& PresetName);
	const struct FMMOCharTemplate* FindTemplateByLabel(const FString& Label) const;
	FString MakeTemplateLabel(const struct FMMOCharTemplate& T) const;
	void UpdatePreviewFromSlots();
	void SetStatus(const FString& Text, bool bError);
};
