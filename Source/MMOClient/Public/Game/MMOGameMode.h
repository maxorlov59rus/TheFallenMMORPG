// Copyright MMOClient Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MMOGameMode.generated.h"

/**
 * Базовый GameMode проекта. В MMO сервер авторитетен — на выделенных
 * серверах используется полностью, на клиенте только для локальных карт
 * (Login/Lobby) без репликации геймплея.
 */
UCLASS()
class MMOCLIENT_API AMMOGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMMOGameMode();
};
