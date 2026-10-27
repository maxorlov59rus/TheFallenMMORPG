// Copyright MMOClient Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MMOCharacterEditorSubsystem.generated.h"

class UMMOConnectionManager;

/** Одна запись шаблона редактора персонажа (раса×класс + допустимые слоты внешности).
 *  Источник истины — БД ogni_characters.char_template (SQL/Core/05_characters_db.sql),
 *  доставка — S2C CharTemplates (0x3003), appearance_schema JSON. */
USTRUCT(BlueprintType)
struct FMMOCharTemplate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	FString TemplateKey;            // 'einherjar_berserker'

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	FString RaceKey;                // einherjar | huldra | leosalfar | duvalfar

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	FString ClassKey;               // berserker | rune_caster | seidhkona

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	FString Description;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	FString StartingZone;

	/** slot -> список допустимых значений (валидируется СЕРВЕРОМ при CharCreate) */
	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	TMap<FString, TArray<FString>> AppearanceSlots;

	/** готовые пресеты ('default','hound_of_odin',...) для кнопки «случайный образ» */
	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	TArray<FString> Presets;

	/** превью-ресурсы по body_preset: key = значение слота, value = спрайт/иконка (заполняет серверный контент-пак) */
	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	TMap<FString, class UTexture2D*> BodyPreviewSprites;
};

/** Запись ростера аккаунта (S2C CharList 0x3002 -> characters[]) */
USTRUCT(BlueprintType)
struct FMMOCharRosterEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	int64 CharacterId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	FString RaceKey;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	FString ClassKey;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	int32 Level = 1;
};

/** Итог WorldReady (0x3013) — стартовое состояние для спавна персонажа в мире */
USTRUCT(BlueprintType)
struct FMMOWorldReadyInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	int64 CharacterId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	FString ZoneKey;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	int32 Hp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	int32 MaxHp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	FString ResourceType;           // saga_rage | blood | soul_particles

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	int32 Resource = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	TMap<FString, int32> Stats;     // might/insight/spirit/vigor/presence
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCharTemplatesReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCharRosterReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCharCreateResult, bool, bSuccess, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWorldReady, const FMMOWorldReadyInfo&, Info);

/**
 * Прослойка «редактор персонажа» между wire-слоем (UMMOConnectionManager) и UI (UMG).
 * Порядок использования (см. Docs/CHARACTER_FLOW.md):
 *   1. RequestRosterAndTemplates()  -> OnCharRosterReady / OnCharTemplatesReady
 *   2. UI редактора собирает {template_key, name, appearance{slot:value}}
 *   3. CreateCharacter(...)         -> OnCharCreateResult (ошибки CHAR_* с сервера)
 *   4. EnterWorld(CharacterId)      -> OnWorldReady -> загрузка карты зоны + спавн
 */
UCLASS()
class MMOCLIENT_API UMMOCharacterEditorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void BindConnection(UMMOConnectionManager* InConn);

	UFUNCTION(BlueprintCallable, Category = "MMO|Char")
	bool RequestRosterAndTemplates(FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "MMO|Char")
	bool CreateCharacter(const FString& TemplateKey, const FString& Name,
	                     const TMap<FString, FString>& Appearance, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "MMO|Char")
	bool EnterWorld(int64 CharacterId, FString& OutError);

	const TArray<FMMOCharTemplate>& GetTemplates() const { return Templates; }
	const TArray<FMMOCharRosterEntry>& GetRoster() const { return Roster; }

	/** ID персонажа, только что созданного через CreateCharacter (0 = ещё не создавали) */
	UFUNCTION(BlueprintPure, Category = "MMO|Char")
	int64 GetLastCreatedCharacterId() const { return LastCreatedCharacterId; }

	UPROPERTY(BlueprintAssignable, Category = "MMO|Char")
	FOnCharTemplatesReady OnCharTemplatesReady;

	UPROPERTY(BlueprintAssignable, Category = "MMO|Char")
	FOnCharRosterReady OnCharRosterReady;

	UPROPERTY(BlueprintAssignable, Category = "MMO|Char")
	FOnCharCreateResult OnCharCreateResult;

	UPROPERTY(BlueprintAssignable, Category = "MMO|Char")
	FOnWorldReady OnWorldReady;

private:
	void HandleWireJson(uint16 MsgId, const FString& JsonUtf8);
	void ParseCharList(const FString& Json);
	void ParseCharTemplates(const FString& Json);
	void ParseCharCreateOk(const FString& Json);
	void ParseWorldReady(const FString& Json);

	/** клиент-side предпроверка против серверной политики (regex/forbidden из name_policy) */
	static bool ValidateNameLocal(const FString& Name, FString& OutReason);

	UPROPERTY()
	TObjectPtr<UMMOConnectionManager> Conn;

	TArray<FMMOCharTemplate> Templates;
	TArray<FMMOCharRosterEntry> Roster;
	int32 MaxCharsPerAccount = 5;
	int64 LastCreatedCharacterId = 0;
};
