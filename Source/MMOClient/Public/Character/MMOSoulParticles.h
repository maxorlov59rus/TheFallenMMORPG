// Copyright MMOClient Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MMOSoulParticles.generated.h"

class UNiagaraSystem;
class UParticleSystem;

/**
 * Визуализация цикла Частиц Душ (Docs/SERVER_ARCHITECTURE.md §3) на клиенте.
 *
 * Триггеры приходят с сервера (soul_gain в SwingResult / loot-события);
 * компонент порождает лёгкий FX у позиции источника и над персонажем.
 * Niagara-ассеты подключаются художником через Properties (EditInlineNew не
 * используется — только ссылки на контента-пакеты, чтобы код не зависел от
 * наличия арта). Если ассет пустой — работает fallback-спрайт из
 * UMMOAssetLibrary::MakeBodyPresetSprite (белая вспышка частицы).
 */
UCLASS(ClassGroup = (MMO), meta = (BlueprintSpawnableComponent))
class MMOCLIENT_API UMMOSoulParticles : public UActorComponent
{
	GENERATED_BODY()

public:
	UMMOSoulParticles();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	/** Сервер подтвердил сбор N частиц у точки Location (SwingResult.soul_gain, loot). */
	UFUNCTION(BlueprintCallable, Category = "MMO|Soul")
	void OnSoulsGathered(const FVector& Location, int32 Count);

	/** Расход частиц на способность (рунический маг / сейдкона): импульс к «кузнечному» узлу. */
	UFUNCTION(BlueprintCallable, Category = "MMO|Soul")
	void OnSoulsExpended(int32 Count);

protected:
	UPROPERTY(EditAnywhere, Category = "MMO|Soul|FX", meta = (DisplayName = "Gather Niagara"))
	TObjectPtr<UNiagaraSystem> GatherNiagara;

	UPROPERTY(EditAnywhere, Category = "MMO|Soul|FX", meta = (DisplayName = "Forge Niagara"))
	TObjectPtr<UNiagaraSystem> ForgeNiagara;

	/** Цвет ауры по расе (задаётся из EditorSubsystem при WorldReady). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|Soul|Tint")
	FLinearColor SoulTint = FLinearColor(0.55f, 0.85f, 1.f, 1.f);

private:
	/** Активные «дрейфующие» частицы для дешёвого CPU-варианта без Niagara. */
	struct FDriftingSoul
	{
		FVector Location;
		FVector Velocity;
		float Age;
		float Life;
	};
	TArray<FDriftingSoul> Drifters;
};
