// Copyright MMOClient Project. All Rights Reserved.

#include "Character/MMOCharacter.h"
#include "MMOClient.h"

AMMOCharacter::AMMOCharacter()
{
	// Сервер авторитетен: на клиенту движение реплицируется минимально
	bReplicates = true;
	SetReplicatingMovement(true);
	NetUpdateFrequency = 30.f;
	MinNetUpdateFrequency = 10.f;
}

void AMMOCharacter::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogMMOClient, Log, TEXT("MMOCharacter BeginPlay: %s"), *DisplayName);
}

void AMMOCharacter::ApplyServerIdentity(const FMMOCharacterId& InId, const FString& InDisplayName)
{
	CharacterId = InId;
	DisplayName = InDisplayName;
}

void AMMOCharacter::SmoothCorrectionToServer(const FVector& ServerLocation, float DeltaSeconds)
{
	const FVector Current = GetActorLocation();
	const float DistSq = FVector::DistSquared(Current, ServerLocation);

	if (DistSq > MaxAllowedDesyncCm * MaxAllowedDesyncCm)
	{
		// Рассинхрон слишком велик — телепорт без интерполяции
		SetActorLocation(ServerLocation);
		UE_LOG(LogMMONet, Warning, TEXT("Hard correction: desync %.0f cm"), FMath::Sqrt(DistSq));
		return;
	}

	const float Alpha = 1.f - FMath::Exp(-CorrectionInterpSpeed * DeltaSeconds);
	SetActorLocation(FMath::Lerp(Current, ServerLocation, Alpha));
}
