// Copyright MMOClient Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Net/MMONetTypes.h"
#include "MMOCharacter.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * Клиентское представление персонажа игрока.
 * Движение отправляется на сервер как намерения (intent),
 * финальная позиция приходит с сервера (server-authoritative).
 */
UCLASS(Config = Game)
class MMOCLIENT_API AMMOCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AMMOCharacter();

	/** Визуальные компоненты-заглушки (до подключения скелетных мешей):
	 *  Body — торс+голова, Legs — ноги. Midtone задаётся по расе через ApplyRaceTint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Visual")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Visual")
	TObjectPtr<UStaticMeshComponent> Legs;

	/** Оттенок расы из char_template.race_key (einherjar/huldra/leosalfar/duvalfar). */
	UFUNCTION(BlueprintCallable, Category = "MMO|Visual")
	void ApplyRaceTint(const FString& RaceKey);

	UFUNCTION(BlueprintCallable, Category = "MMO|Identity")
	void ApplyServerIdentity(const FMMOCharacterId& InId, const FString& InDisplayName);

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Identity")
	FMMOCharacterId CharacterId;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Identity")
	FString DisplayName;

protected:
	virtual void BeginPlay() override;

	/** Плавная интерполяция к серверной позиции при рассинхроне */
	void SmoothCorrectionToServer(const FVector& ServerLocation, float DeltaSeconds);

	UPROPERTY(EditAnywhere, Config, Category = "MMO|Net")
	float CorrectionInterpSpeed = 8.f;

	/** Максимально допустимое отклонение до принудительного телепорта (античит-фильтр) */
	UPROPERTY(EditAnywhere, Config, Category = "MMO|Net")
	float MaxAllowedDesyncCm = 500.f;
};
