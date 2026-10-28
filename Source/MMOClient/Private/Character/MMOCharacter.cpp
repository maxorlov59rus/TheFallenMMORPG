// Copyright MMOClient Project. All Rights Reserved.

#include "Character/MMOCharacter.h"
#include "MMOClient.h"
#include "Components/StaticMeshComponent.h"
#include "Game/MMOAssetLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

AMMOCharacter::AMMOCharacter()
{
	// Визуальные заглушки: примитивы-боксы вместо мешей художника.
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent); // заглушки к капсуле (GetMesh пуст без скелета)
	Body->SetRelativeScale3D(FVector(0.55f, 0.35f, 0.9f));
	Body->SetRelativeLocation(FVector(0.f, 0.f, 40.f));

	Legs = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Legs"));
	Legs->SetupAttachment(RootComponent);
	Legs->SetRelativeScale3D(FVector(0.4f, 0.3f, 0.8f));
	Legs->SetRelativeLocation(FVector(0.f, 0.f, -45.f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BoxMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (BoxMesh.Succeeded())
	{
		Body->SetStaticMesh(BoxMesh.Object);
		Legs->SetStaticMesh(BoxMesh.Object);
	}
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
void AMMOCharacter::ApplyRaceTint(const FString& RaceKey)
{
	const FLinearColor Tint = UMMOAssetLibrary::ColorFromStringKey(RaceKey, 0.5f, 0.8f);
	UMaterialInterface* BaseMat = UMMOAssetLibrary::ResolveWorldMaterial();
	if (BaseMat)
	{
		UMaterialInstanceDynamic* MID = nullptr;
		MID = Body->CreateDynamicMaterialInstance(0, BaseMat);
		if (MID) MID->SetVectorParameterValue(TEXT("Midtone"), Tint);
		Body->SetMaterial(0, MID);
		Legs->SetMaterial(0, MID);
	}
	else if (UMaterialInstanceDynamic* MID = Body->CreateDynamicMaterialInstance(0))
	{
		// Без материала художника — базовый MID с debug-оттенком расы
		MID->SetVectorParameterValue(FName("Base Color"), Tint);
		Body->SetMaterial(0, MID);
		if (UMaterialInstanceDynamic* MID2 = Legs->CreateDynamicMaterialInstance(0))
		{
			MID2->SetVectorParameterValue(FName("Base Color"), Tint);
			Legs->SetMaterial(0, MID2);
		}
	}
}
