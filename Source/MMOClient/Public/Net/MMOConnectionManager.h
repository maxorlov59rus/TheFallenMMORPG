// Copyright MMOClient Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Net/MMONetTypes.h"
#include "Net/MMOWire.h"
#include "MMOConnectionManager.generated.h"

class UNetConnection;
class USocketSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConnectionStateChanged,
	EMMOConnectionState, NewState);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWireJsonMessage,
	uint16, MsgId, const FString&, JsonUtf8);

/**
 * Управляет подключением клиента к игровому серверу:
 * connect -> auth handshake -> enter world -> reconnect on drop.
 * Server-authoritative: клиент только отправляет намерения и принимает состояние.
 */
UCLASS(BlueprintType)
class MMOCLIENT_API UMMOConnectionManager : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MMO|Net")
	void ConnectToServer(const FString& Host, uint32 Port, const FString& AuthToken);

	UFUNCTION(BlueprintCallable, Category = "MMO|Net")
	void Disconnect();

	UPROPERTY(BlueprintAssignable, Category = "MMO|Net")
	FOnConnectionStateChanged OnConnectionStateChanged;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Net")
	EMMOConnectionState ConnectionState = EMMOConnectionState::Disconnected;

	// ---------- Персонажная линия (редактор создания) для UI ----------
	/** Вызывать из Tick владельца (GameInstance/TickOwner) каждый кадр */
	UFUNCTION(BlueprintCallable, Category = "MMO|Net")
	void Tick(float DeltaSeconds);

	/** C2S CharListReq{} — ростер аккаунта + сводка шаблонов */
	UFUNCTION(BlueprintCallable, Category = "MMO|Char")
	bool RequestCharacterList(FString& OutError);

	/** C2S CharTemplates (триггер) — полный appearance_schema для редактора */
	UFUNCTION(BlueprintCallable, Category = "MMO|Char")
	bool RequestCharTemplates(FString& OutError);

	/** C2S CharCreate{template_key,name,appearance} — сервер валидирует имя/слоты/лимит */
	UFUNCTION(BlueprintCallable, Category = "MMO|Char")
	bool CreateCharacter(const FString& TemplateKey, const FString& Name,
	                     const TMap<FString, FString>& Appearance, FString& OutError);

	/** C2S CharEnterWorld{character_id} — ownership-проверка на сервере, затем WorldReady */
	UFUNCTION(BlueprintCallable, Category = "MMO|Char")
	bool EnterWorldWithCharacter(int64 CharacterId, FString& OutError);

	/** Сырые S2C JSON-кадры персонажной линии (MsgId: 0x3002 CharList, 0x3003 Templates,
	 *  0x3011 CharCreateOk, 0x3013 WorldReady). Парсит UMMOCharacterEditorSubsystem. */
	UPROPERTY(BlueprintAssignable, Category = "MMO|Char")
	FOnWireJsonMessage OnWireJsonMessage;

	UPROPERTY(BlueprintReadOnly, Category = "MMO|Char")
	int64 ActiveCharacterId = 0;

protected:
	void SetState(EMMOConnectionState NewState);

	void BeginConnect();
	void HandleConnectionError();
	void HandleSuccessfulConnection(UNetConnection* Connection);

	/** опрос ogni-wire handshake (кадр в секунду из TickOwner-мира) */
	void TickWireHandshake(float DeltaSeconds);
	void OnWireAccepted();
	void OnWireRejected(const TCHAR* Reason);
	void DispatchWireJson(uint16 MsgId, const FString& JsonUtf8);
	bool SendCharFrame(OgnWire::EMsgId MsgId, const FString& JsonBody, FString& OutError);

	FString ServerHost;
	uint32 ServerPort = 7777;
	FString PendingAuthToken;

	/** UDP-транспорт C2S_SessionHello -> S2C_SessionAccept (ogni_wire фрейминг) */
	TUniquePtr<FMMOWireLink> WireLink;

	/** Session key, полученный при handshake; для шифрования надёжного слоя (след. итерация) */
	TArray<uint8> SessionKey;
	uint32 AoiSeed = 0;

	/** Повторные попытки с экспоненциальным backoff */
	int32 RetryCount = 0;
	static constexpr int32 MaxRetries = 5;
	float CurrentRetryDelaySeconds = 1.f;
};
