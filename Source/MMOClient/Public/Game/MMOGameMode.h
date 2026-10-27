// Copyright MMOClient Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MMOGameMode.generated.h"

/**
 * Клиентский GameMode. Архитектура MMO server-authoritative: игровые миры
 * управляются выделенными серверами, поэтому AMMOGameMode существует только
 * на локальных картах клиента (Login / Lobby / Loading), где репликация
 * геймплея не требуется. Здесь он управляет UI-переходами и стартовым состоянием.
 */
UCLASS(Config = Game)
class MMOCLIENT_API AMMOGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMMOGameMode();

	/** Идентификатор зоны/мира из конфигурации карты (используется при travel-логинге) */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "MMO|World")
	FName ZoneId = NAME_None;
};
