// Copyright MMOClient Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Character/MMOCharacterEditorSubsystem.h"
#include "MMOSoulHUD.generated.h"

class UFont;

/**
 * Серверный HUD: nameplate'ы персонажей + полосы здоровья/ресурса.
 * Отрисовка immediate-mode (Canvas), без UMG-виджетов на каждого моба —
 * в экшен-MMORPG с сотнями entity это дешевле и не зависит от наличия арта.
 *
 * Данные приходят из wire-слоя: WorldReady заполняет LocalPlayerInfo,
 * SwingResult/MobSnapshot обновляют удалённые entity (см. Docs/PROTOCOL.md).
 */
UCLASS()
class MMOCLIENT_API AMMOSoulHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** Регистрация локального игрока после WorldReady (имя, раса/класс для цвета). */
	UFUNCTION(BlueprintCallable, Category = "MMO|HUD")
	void SetLocalPlayerInfo(const FMMOWorldReadyInfo& Info);

	/** Обновление полос чужой сущности (вызывается из обработчика S2C-снапшотов). */
	UFUNCTION(BlueprintCallable, Category = "MMO|HUD")
	void UpdateEntityBars(int64 EntityId, const FString& Label, float HpFrac, float ResFrac, int32 MaxHp);

protected:
	UPROPERTY(EditAnywhere, Category = "MMO|HUD")
	TObjectPtr<UFont> Font = nullptr;

	UPROPERTY(EditAnywhere, Category = "MMO|HUD")
	int32 FontSize = 18;

private:
	struct FEntityBar
	{
		FString Label;
		float HpFrac = 1.f;
		float ResFrac = 0.f;
		double LastSeen = 0.0;
	};

	TMap<int64, FEntityBar> Entities;
	FName LocalPlayerNameKey = NAME_None; // имя локального игрока (для nameplate над собой)

	void DrawBar(const FVector2D& ScreenPos, float HpFrac, float ResFrac, const FString& Label);
};
