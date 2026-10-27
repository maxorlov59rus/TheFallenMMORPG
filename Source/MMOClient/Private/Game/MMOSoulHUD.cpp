// Copyright MMOClient Project. All Rights Reserved.

#include "Game/MMOSoulHUD.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Game/MMOAssetLibrary.h"

void AMMOSoulHUD::SetLocalPlayerInfo(const FMMOWorldReadyInfo& Info)
{
	LocalPlayerNameKey = FName(*Info.Name);
}

void AMMOSoulHUD::UpdateEntityBars(int64 EntityId, const FString& Label, float HpFrac, float ResFrac, int32 MaxHp)
{
	FEntityBar& Bar = Entities.FindOrAdd(EntityId);
	Bar.Label = Label;
	Bar.HpFrac = FMath::Clamp(HpFrac, 0.f, 1.f);
	Bar.ResFrac = FMath::Clamp(ResFrac, 0.f, 1.f);
	Bar.LastSeen = GetWorld()->GetTimeSeconds();
}

void AMMOSoulHUD::DrawHUD()
{
	Super::DrawHUD();

	const double Now = GetWorld()->GetTimeSeconds();
	// Чистим stale-записи (AOI вышел из видимости > 5с).
	for (auto It = Entities.CreateIterator(); It; ++It)
	{
		if (Now - It->Value.LastSeen > 5.0) It.RemoveCurrent();
	}

	UFont* UseFont = Font ? Font : GEngine->GetSmallFont();
	int32 Idx = 0;
	for (const auto& Pair : Entities)
	{
		if (Idx >= 10) break; // cap на экранный список (dev-раскладка)
		const FEntityBar& Bar = Pair.Value;
		// TODO(AOI): проекция world->screen по актору сущности после подключения
		// реестра entity-акторов (Docs/PROTOCOL.md MobSnapshot). До этого — стабильная
		// сетка слева сверху: читаемость боевой информации важнее точности привязки.
		const FVector2D Pos(24.f, 120.f + Idx * 54.f);
		DrawBar(Pos, Bar.HpFrac, Bar.ResFrac, Bar.Label);
		++Idx;
	}

	// Nameplate локального игрока — под прицелом по центру экрана (dev-раскладка).
	if (!LocalPlayerNameKey.IsNone())
	{
		const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY - 160.f);
		SetTextColor(FLinearColor::White);
		if (UseFont)
			DrawText(LocalPlayerNameKey.ToString(), UseFont, Center.X - 40.f, Center.Y, nullptr, nullptr, FontSize);
	}
}

void AMMOSoulHUD::DrawBar(const FVector2D& ScreenPos, float HpFrac, float ResFrac, const FString& Label)
{
	constexpr float W = 160.f, H = 10.f, Gap = 3.f;

	// Фон
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), ScreenPos.X, ScreenPos.Y, W, H);
	// HP — тёплый янтарный (эстетика «огней душ»)
	const FLinearColor HpColor = FLinearColor::LerpUsingHSV(
		FLinearColor(0.85f, 0.15f, 0.1f), FLinearColor(0.95f, 0.65f, 0.2f), HpFrac);
	DrawRect(HpColor, ScreenPos.X, ScreenPos.Y, W * HpFrac, H);
	// Ресурс (сага-ярость / кровь / частицы душ) — холодный cyan
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), ScreenPos.X, ScreenPos.Y + H + Gap, W, H * 0.6f);
	DrawRect(FLinearColor(0.35f, 0.75f, 0.95f), ScreenPos.X, ScreenPos.Y + H + Gap, W * ResFrac, H * 0.6f);
	// Рамка
	DrawRect(FLinearColor(0.9f, 0.85f, 0.7f, 0.9f), ScreenPos.X - 1.f, ScreenPos.Y - 1.f, W + 2.f, 1.f);

	UFont* UseFont = Font ? Font : GEngine->GetSmallFont();
	if (UseFont)
	{
		SetTextColor(FLinearColor::White);
		DrawText(Label, UseFont, ScreenPos.X, ScreenPos.Y - 18.f, nullptr, nullptr, FontSize - 2);
	}
}
