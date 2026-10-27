// Copyright MMOClient Project. All Rights Reserved.

#include "Net/MMOConnectionManager.h"
#include "MMOClient.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "OnlineSubsystemUtils.h"

void UMMOConnectionManager::ConnectToServer(const FString& Host, uint32 Port, const FString& AuthToken)
{
	ServerHost = Host;
	ServerPort = Port;
	PendingAuthToken = AuthToken;
	RetryCount = 0;
	CurrentRetryDelaySeconds = 1.f;

	BeginConnect();
}

void UMMOConnectionManager::BeginConnect()
{
	SetState(EMMOConnectionState::Connecting);

	const FString URLStr = FString::Printf(TEXT("%s:%u?auth=%s"), *ServerHost, ServerPort, *PendingAuthToken);

	// Клиентское подключение к выделенному игровому серверу через Iris Net Driver.
	// Точный вызов зависит от API конкретной сборки UE6: обычно это
	// APlayerController::ClientTravel(*URLStr, TRAVEL_Absolute) либо
	// UGameInstance-обёртка над UNetDriver::Connect. Здесь оставлен driver-based путь.
	UWorld* World = GetWorld();
	UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	if (!Driver)
	{
		UE_LOG(LogMMONet, Error, TEXT("No NetDriver available for client connect"));
		HandleConnectionError();
		return;
	}

	Driver->Connect(FURL(nullptr, *URLStr), TEXT(""), nullptr /*NoPackage*/, false /*bSeamless*/);
	UE_LOG(LogMMONet, Log, TEXT("Connecting to %s:%u"), *ServerHost, ServerPort);
}

void UMMOConnectionManager::Disconnect()
{
	SetState(EMMOConnectionState::Disconnected);
	// TODO: close active NetConnection / notify GameInstance shutdown path
}

void UMMOConnectionManager::HandleSuccessfulConnection(UNetConnection* Connection)
{
	UE_LOG(LogMMONet, Log, TEXT("Connected to server, starting auth handshake"));
	SetState(EMMOConnectionState::Authenticating);
	// Handshake по RPC-протоколу: клиент шлёт токен, сервер валидирует и отвечает EnterWorld
}

void UMMOConnectionManager::HandleConnectionError()
{
	if (RetryCount >= MaxRetries)
	{
		UE_LOG(LogMMONet, Warning, TEXT("Connection failed after %d retries"), MaxRetries);
		SetState(EMMOConnectionState::Failed);
		return;
	}

	++RetryCount;
	CurrentRetryDelaySeconds *= 2.f; // экспоненциальный backoff: 1,2,4,8,16 сек
	UE_LOG(LogMMONet, Warning, TEXT("Connection lost. Retry %d in %.1fs"), RetryCount, CurrentRetryDelaySeconds);
	SetState(EMMOConnectionState::Reconnecting);

	// TODO: FTimerManager::SetTimer -> BeginConnect() через CurrentRetryDelaySeconds
}

void UMMOConnectionManager::SetState(EMMOConnectionState NewState)
{
	if (ConnectionState != NewState)
	{
		ConnectionState = NewState;
		OnConnectionStateChanged.Broadcast(NewState);
	}
}
