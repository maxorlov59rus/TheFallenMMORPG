// Copyright MMOClient Project. All Rights Reserved.

#include "Net/MMOConnectionManager.h"
#include "MMOClient.h"
#include "Character/MMOCharacterEditorSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

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

	// ogni-wire handshake по UDP на world_port (mirrors Server/tools/worldserver_ref.py).
	// Iris NetDriver-подключение (боевой путь репликации) добавляется после WorldReady.
	if (!WireLink)
	{
		WireLink = MakeUnique<FMMOWireLink>(this);
		WireLink->SetJsonHandler(FMMOWireLink::FOnWireJsonNative::CreateUObject(
			this, &UMMOConnectionManager::DispatchWireJson));
	}

	FString Err;
	if (!WireLink->Open(ServerHost, ServerPort, Err))
	{
		UE_LOG(LogMMONet, Error, TEXT("Wire link open failed: %s"), *Err);
		HandleConnectionError();
		return;
	}
	WireLink->SendHello(PendingAuthToken);
	SetState(EMMOConnectionState::Authenticating);
	UE_LOG(LogMMONet, Log, TEXT("Connecting to %s:%u via ogni-wire"), *ServerHost, ServerPort);
}

void UMMOConnectionManager::Tick(float DeltaSeconds)
{
	TickWireHandshake(DeltaSeconds);
}

void UMMOConnectionManager::Disconnect()
{
	if (WireLink)
	{
		WireLink->Close();
	}
	SetState(EMMOConnectionState::Disconnected);
}

void UMMOConnectionManager::HandleSuccessfulConnection(UNetConnection* Connection)
{
	// Legacy NetDriver path — не используется: авторизация идёт через FMMOWireLink.
	UE_LOG(LogMMONet, Warning, TEXT("HandleSuccessfulConnection: unused NetDriver path"));
}

void UMMOConnectionManager::TickWireHandshake(float DeltaSeconds)
{
	if (!WireLink || !WireLink->IsOpen()) return;
	if (ConnectionState != EMMOConnectionState::Authenticating) return;

	bool bAccepted = false, bRejected = false, bTimedOut = false;
	WireLink->Poll(bAccepted, bRejected, bTimedOut);

	if (bAccepted)
	{
		OnWireAccepted();
	}
	else if (bRejected)
	{
		OnWireRejected(TEXT("session rejected by world server (bad jwt / revoked / build mismatch)"));
	}
	else if (bTimedOut)
	{
		WireLink->SendHello(PendingAuthToken); // ретрай hello (seq инкрементирован внутри Poll)
	}
}

void UMMOConnectionManager::OnWireAccepted()
{
	SessionKey = WireLink->LastSessionKey();
	AoiSeed = WireLink->LastAoiSeed();
	UE_LOG(LogMMONet, Log, TEXT("[wire] S2C_SessionAccept OK (key %d bytes, aoi_seed %X)"),
		SessionKey.Num(), AoiSeed);
	SetState(EMMOConnectionState::ConnectedToWorld);

	// Автоподписка редактора персонажа: GameInstance-подсистема связывается с этим Conn
	// и сама запрашивает ростер/шаблоны (CHARACTER_FLOW п.1, шаг CharListReq).
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMMOCharacterEditorSubsystem* EditorSub = GI->GetSubsystem<UMMOCharacterEditorSubsystem>())
		{
			EditorSub->BindConnection(this);
			FString Err;
			EditorSub->RequestRosterAndTemplates(Err);
		}
	}
	// UI-слой (редактор персонажа) получает ростер по запросу RequestCharacterList()
}

void UMMOConnectionManager::OnWireRejected(const TCHAR* Reason)
{
	UE_LOG(LogMMONet, Warning, TEXT("[wire] handshake rejected: %s"), Reason);
	SetState(EMMOConnectionState::Failed);
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

// ---------- Персонажная линия: C2S JSON-кадры поверх established wire-link ----------

bool UMMOConnectionManager::SendCharFrame(OgnWire::EMsgId MsgId, const FString& JsonBody, FString& OutError)
{
	if (!WireLink || ConnectionState != EMMOConnectionState::ConnectedToWorld)
	{
		OutError = TEXT("wire session not established");
		return false;
	}
	return WireLink->SendJson(MsgId, JsonBody, OutError);
}

bool UMMOConnectionManager::RequestCharacterList(FString& OutError)
{
	return SendCharFrame(OgnWire::EMsgId::CharListReq, TEXT("{}"), OutError);
}

bool UMMOConnectionManager::RequestCharTemplates(FString& OutError)
{
	// msg_id 0x3003 BOTH: пустой объект = триггер полного appearance_schema
	return SendCharFrame(OgnWire::EMsgId::CharTemplates, TEXT("{}"), OutError);
}

bool UMMOConnectionManager::CreateCharacter(const FString& TemplateKey, const FString& Name,
	const TMap<FString, FString>& Appearance, FString& OutError)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("template_key"), TemplateKey);
	Root->SetStringField(TEXT("name"), Name);
	TSharedPtr<FJsonObject> App = MakeShared<FJsonObject>();
	for (const auto& KV : Appearance)
	{
		App->SetStringField(KV.Key, KV.Value);
	}
	Root->SetObjectField(TEXT("appearance"), App);

	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		OutError = TEXT("json serialize failed");
		return false;
	}
	return SendCharFrame(OgnWire::EMsgId::CharCreate, Json, OutError);
}

bool UMMOConnectionManager::EnterWorldWithCharacter(int64 CharacterId, FString& OutError)
{
	const FString Json = FString::Printf(TEXT("{\"character_id\":%lld}"), CharacterId);
	return SendCharFrame(OgnWire::EMsgId::CharEnterWorld, Json, OutError);
}

void UMMOConnectionManager::DispatchWireJson(uint16 MsgId, const FString& JsonUtf8)
{
	// ActiveCharacterId ведём по кадровым маркерам сервера (CharCreateOk / WorldReady)
	if (MsgId == static_cast<uint16>(OgnWire::EMsgId::CharCreateOk) ||
	    MsgId == static_cast<uint16>(OgnWire::EMsgId::WorldReady))
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonUtf8);
		if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
		{
			double Cid = 0.0;
			if (Obj->TryGetNumberField(TEXT("character_id"), Cid))
			{
				ActiveCharacterId = static_cast<int64>(Cid);
			}
		}
	}
	OnWireJsonMessage.Broadcast(MsgId, JsonUtf8);
}
