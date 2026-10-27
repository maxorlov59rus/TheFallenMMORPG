// Copyright MMOClient Project. All Rights Reserved.

#include "Character/MMOSoulParticles.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "Game/MMOAssetLibrary.h"
#include "MMOClient.h"

UMMOSoulParticles::UMMOSoulParticles()
{
	PrimaryComponentTick.bCanEverTick = true;
	bWantsInitializeComponentFromTemplate = false;
}

void UMMOSoulParticles::OnSoulsGathered(const FVector& Location, int32 Count)
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	const int32 Clamped = FMath::Clamp(Count, 1, 16);

	// Основной путь: Niagara-система от художника.
	if (GatherNiagara)
	{
#if WITH_ENGINE
		UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), GatherNiagara, Location, FRotator::ZeroRotator,
			FVector(Clamped * 0.15f + 0.7f), true, true);
		if (NC) NC->SetLinearColor(SoulTint);
#endif
	}

	// Дешёвый CPU-резерв: дрейфующие спрайты-частицы (всегда, даже с Niagara —
	// дают читаемый «полёт душ» к кузнице на дальних дистанциях).
	for (int32 i = 0; i < Clamped; ++i)
	{
		FDriftingSoul S;
		S.Location = Location + FVector(FMath::RandRange(-40.f, 40.f), FMath::RandRange(-40.f, 40.f), FMath::RandRange(20.f, 60.f));
		const FVector ToOwner = Owner->GetActorLocation() - S.Location;
		S.Velocity = ToOwner.GetSafeNormal() * (120.f + FMath::RandRange(0.f, 80.f));
		S.Age = 0.f;
		S.Life = static_cast<float>(Clamped) * 0.08f + 0.6f;
		Drifters.Add(S);
	}
}

void UMMOSoulParticles::OnSoulsExpended(int32 Count)
{
	AActor* Owner = GetOwner();
	if (!Owner || !ForgeNiagara) return;
#if WITH_ENGINE
	if (UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAttached(
		ForgeNiagara, Owner->GetRootComponent(), NAME_None,
		FVector(0, 0, 90.f), FRotator::ZeroRotator, EAttachLocation::KeepOffset, true))
	{
		NC->SetLinearColor(SoulTint);
	}
#endif
}

void UMMOSoulParticles::TickComponent(float DeltaTime, ELevelTick TickType,
                                      FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Owner = GetOwner();
	if (!Owner) return;

	const FVector Target = Owner->GetActorLocation() + FVector(0, 0, 80.f);
	for (int32 i = Drifters.Num() - 1; i >= 0; --i)
	{
		FDriftingSoul& S = Drifters[i];
		S.Age += DeltaTime;
		if (S.Age >= S.Life) { Drifters.RemoveAtSwap(i); continue; }

		// Сходимся к цели с ускорением; последние 20% жизни — поглощение.
		const FVector Dir = (Target - S.Location).GetSafeNormal();
		S.Velocity = Dir * FMath::Lerp(140.f, 420.f, S.Age / S.Life);
		S.Location += S.Velocity * DeltaTime;
	}
	// Отрисовка резервных частиц выполняется material-instance'ами на стороне BP
	// (см. Docs/CONTENT_GUIDE.md): здесь остаётся только симуляция позиций.
}
