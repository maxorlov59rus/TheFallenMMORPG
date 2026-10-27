// Copyright MMOClient Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Net/MMONetTypes.h"
#include "MMOConnectionManager.generated.h"

class UNetConnection;
class USocketSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConnectionStateChanged,
	EMMOConnectionState, NewState);

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

protected:
	void SetState(EMMOConnectionState NewState);

	void BeginConnect();
	void HandleConnectionError();
	void HandleSuccessfulConnection(UNetConnection* Connection);

	FString ServerHost;
	uint32 ServerPort = 7777;
	FString PendingAuthToken;

	/** Повторные попытки с экспоненциальным backoff */
	int32 RetryCount = 0;
	static constexpr int32 MaxRetries = 5;
	float CurrentRetryDelaySeconds = 1.f;
};
