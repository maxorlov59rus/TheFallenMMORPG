// Copyright MMOClient Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MMOAssetLibrary.generated.h"

class UMaterialInterface;
class UTexture2D;

/**
 * Библиотека процедурной графики «Огни душ Падших Валькирий».
 *
 * Назначение: дать пайплайну раннего этапа (до появления художественного контента)
 * корректные заглушки-ассеты, генерируемые на лету. Всё, что здесь создаётся,
 * заменяется реальным арт-контентом без изменения вызывающего кода:
 * достаточно в UMG/акторных компонентах подменить ObjectPath или присвоить
 * asset напрямую (парсеры везде идут через TryLoad, а при промахе — сюда).
 */
UCLASS()
class MMOCLIENT_API UMMOAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Процедурная PBR-заглушка скалы/металла для мира (WorldValidatedMaterial).
	 * Path: /MMOClient/Procedural/M_WorldValidated.M_WorldValidated
	 * Если материал не найден — возвращает nullptr (вызывающий рисует debug-цвет).
	 */
	UFUNCTION(BlueprintCallable, Category = "MMO|Assets", meta = (DisplayName = "Resolve World Material"))
	static UMaterialInterface* ResolveWorldMaterial();

	/**
	 * Процедурный спрайт силуэта тела по ключу body_preset из БД шаблонов
	 * ('default','hound_of_odin',...). Используется редактором персонажа как
	 * fallback-превью, когда BodyPresetSprites в WBP не заполнен художником.
	 * Возвращает временную UTexture2D (не сериализуется — только для UI).
	 */
	UFUNCTION(BlueprintCallable, Category = "MMO|Assets", meta = (DisplayName = "Make Body Preset Sprite"))
	static UTexture2D* MakeBodyPresetSprite(const FString& BodyPresetKey, FLinearColor Tint);

	/** Стабильный цвет по хэшу строки (раса/класс/имя) — для иконок, nameplate'ов, миникарты. */
	UFUNCTION(BlueprintPure, Category = "MMO|Assets")
	static FLinearColor ColorFromStringKey(const FString& Key, float Saturation = 0.55f, float Value = 0.85f);
};
