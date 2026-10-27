// Copyright MMOClient Project. All Rights Reserved.

#include "Game/MMOAssetLibrary.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "MMOClient.h"

UMaterialInterface* UMMOAssetLibrary::ResolveWorldMaterial()
{
	// Реальный ассет появляется на этапе сборки контента (см. Content/README.md).
	UMaterialInterface* Mat = Cast<UMaterialInterface>(
		TryLoadObject<UMaterialInterface>(nullptr, TEXT("/MMOClient/Procedural/M_WorldValidated.M_WorldValidated")));
	if (!Mat)
	{
		UE_LOG(LogMMOClient, Verbose, TEXT("M_WorldValidated not found — callers fall back to procedural colors"));
	}
	return Mat;
}

FLinearColor UMMOAssetLibrary::ColorFromStringKey(const FString& Key, float Saturation, float Value)
{
	// Детерминированный hue по хэшу строки; одинаковый ключ => одинаковый цвет всегда.
	uint32 H = FCrc::StrCrc32(*Key);
	const float Hue = static_cast<float>(H % 360u);
	return FLinearColor::MakeFromHSV8(
		static_cast<uint8>(Hue * 255.f / 360.f),
		static_cast<uint8>(Saturation * 255.f),
		static_cast<uint8>(Value * 255.f));
}

UTexture2D* UMMOAssetLibrary::MakeBodyPresetSprite(const FString& BodyPresetKey, FLinearColor Tint)
{
	// Силуэт ~48x64, генерируется по ключу пресета: пропорции меняются детерминированно.
	constexpr int32 W = 48;
	constexpr int32 H = 64;
	TArray<FColor> Pixels;
	Pixels.Init(FColor(0, 0, 0, 0), W * H);

	uint32 Hsh = FCrc::StrCrc32(*BodyPresetKey);
	const float BuildScale   = 0.75f + static_cast<float>(Hsh % 100u) / 100.f * 0.5f;      // 0.75..1.25
	const float ShoulderHalf = (W * 0.22f) * BuildScale;
	const float WaistHalf    = (W * 0.15f) * BuildScale;
	const int32 HeadR        = FMath::Max(5, static_cast<int32>(W * 0.13f * BuildScale));
	const int32 NeckY        = 10 + static_cast<int32>((Hsh >> 8) % 4u);                     // посадка головы
	const int32 BodyTop      = NeckY + HeadR;
	const int32 BodyBottom   = H - 4;
	const FLinearColor Silhouette = Tint * FLinearColor(0.9f, 0.9f, 0.95f, 1.f);

	auto BlendPixel = [&](int32 X, int32 Y, float A)
	{
		if (X < 0 || Y < 0 || X >= W || Y >= H || A <= 0.f) return;
		FColor& Dst = Pixels[Y * W + X];
		const uint8 SrcA = static_cast<uint8>(FMath::Clamp(A, 0.f, 1.f) * 255.f);
		Dst.R = static_cast<uint8>(Silhouette.R * 255.f);
		Dst.G = static_cast<uint8>(Silhouette.G * 255.f);
		Dst.B = static_cast<uint8>(Silhouette.B * 255.f);
		Dst.A = FMath::Max(Dst.A, SrcA);
	};

	// Голова — круг
	for (int32 Y = 0; Y < NeckY + HeadR * 2; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const float DX = X - W * 0.5f;
			const float DY = Y - (NeckY + HeadR);
			const float D  = FMath::Sqrt(DX * DX + DY * DY);
			if (D <= HeadR + 0.5f) BlendPixel(X, Y, 1.f - FMath::Max(0.f, D - HeadR + 0.5f));
		}
	}
	// Торс — трапеция плечи->талия с мягкими краями
	const float Grad = static_cast<float>(BodyBottom - BodyTop);
	for (int32 Y = BodyTop; Y <= BodyBottom; ++Y)
	{
		const float T = (Y - BodyTop) / Grad;
		const float HalfW = FMath::Lerp(ShoulderHalf, WaistHalf, T);
		for (int32 X = 0; X < W; ++X)
		{
			const float DX = FMath::Abs(X - W * 0.5f);
			if (DX <= HalfW) BlendPixel(X, Y, 1.f);
			else if (DX <= HalfW + 1.f) BlendPixel(X, Y, (HalfW + 1.f - DX));
		}
	}

	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!Tex) return nullptr;
	void* Data = Tex->Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), W * H * sizeof(FColor));
	Tex->Unlock();
	Tex->Filter = TF_Bilinear;
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	return Tex;
}
