// Copyright MMOClient Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MMONetTypes.generated.h"

/** Идентификатор персонажа, выдаётся сервером при входе в мир */
USTRUCT(BlueprintType)
struct FMMOCharacterId
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int64 Id = 0;

	bool IsValid() const { return Id != 0; }
	bool operator==(const FMMOCharacterId& Other) const { return Id == Other.Id; }
};

/** Компактные базовые характеристики для HUD/тултипов */
USTRUCT(BlueprintType)
struct FMMOCharacterStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	float Health = 100.f;

	UPROPERTY(BlueprintReadOnly)
	float MaxHealth = 100.f;

	UPROPERTY(BlueprintReadOnly)
	float Mana = 50.f;

	UPROPERTY(BlueprintReadOnly)
	float MaxMana = 50.f;

	UPROPERTY(BlueprintReadOnly)
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly)
	int64 Experience = 0;
};

/** Представление сущности в мире (игрок/NPC), приходит с сервера */
USTRUCT(BlueprintType)
struct FMMOWorldEntity
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FMMOCharacterId Id;

	UPROPERTY(BlueprintReadOnly)
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantize Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly)
	FName MeshKey = NAME_None;

	UPROPERTY(BlueprintReadOnly)
	FMMOCharacterStats Stats;
};

/** Типы событий net-протокола клиента */
UENUM(BlueprintType)
enum class EMMOConnectionState : uint8
{
	Disconnected,
	Connecting,
	Authenticating,
	ConnectedToWorld,
	Reconnecting,
	Failed
};
