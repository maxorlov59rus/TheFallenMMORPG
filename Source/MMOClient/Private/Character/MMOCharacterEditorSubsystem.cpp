// Copyright MMOClient Project. All Rights Reserved.

#include "Character/MMOCharacterEditorSubsystem.h"
#include "Net/MMOConnectionManager.h"
#include "MMOClient.h"
#include "Dom/Array.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// wire msg_id (зеркало OgnWire::EMsgId / ogni_wire.py)
static constexpr uint16 MsgCharList      = 0x3002;
static constexpr uint16 MsgCharTemplates = 0x3003;
static constexpr uint16 MsgCharCreateOk  = 0x3011;
static constexpr uint16 MsgWorldReady    = 0x3013;

void UMMOCharacterEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogMMOClient, Log, TEXT("[char-editor] subsystem up"));
}

void UMMOCharacterEditorSubsystem::Deinitialize()
{
	if (Conn)
	{
		Conn->OnWireJsonMessage.RemoveDynamic(this, &UMMOCharacterEditorSubsystem::HandleWireJson);
	}
	Super::Deinitialize();
}

void UMMOCharacterEditorSubsystem::BindConnection(UMMOConnectionManager* InConn)
{
	if (Conn == InConn) return;
	if (Conn)
	{
		Conn->OnWireJsonMessage.RemoveDynamic(this, &UMMOCharacterEditorSubsystem::HandleWireJson);
	}
	Conn = InConn;
	if (Conn)
	{
		Conn->OnWireJsonMessage.AddDynamic(this, &UMMOCharacterEditorSubsystem::HandleWireJson);
	}
}

bool UMMOCharacterEditorSubsystem::RequestRosterAndTemplates(FString& OutError)
{
	if (!Conn) { OutError = TEXT("connection not bound"); return false; }
	FString E1, E2;
	const bool bList = Conn->RequestCharacterList(E1);
	const bool bTpl  = Conn->RequestCharTemplates(E2);
	if (!bList || !bTpl)
	{
		OutError = !bList ? E1 : E2;
		return false;
	}
	return true;
}

bool UMMOCharacterEditorSubsystem::ValidateNameLocal(const FString& Name, FString& OutReason)
{
	// зеркало name_policy: ^[A-Z][a-zA-Z]{1,23}$ + forbidden_words (полный список приходит с сервером)
	if (Name.Len() < 2 || Name.Len() > 24)
	{
		OutReason = TEXT("Имя должно быть 2–24 символа");
		return false;
	}
	if (!FChar::IsUpper(Name[0]))
	{
		OutReason = TEXT("Имя должно начинаться с заглавной буквы");
		return false;
	}
	for (int32 i = 1; i < Name.Len(); ++i)
	{
		if (!FChar::IsAlpha(Name[i]))
		{
			OutReason = TEXT("Имя может содержать только латинские буквы");
			return false;
		}
	}
	static const TArray<FString> Forbidden = {
		TEXT("odin"), TEXT("vala"), TEXT("sigrid"), TEXT("fallen"),
		TEXT("wlkv"), TEXT("admin"), TEXT("gm"), TEXT("test") };
	if (Forbidden.Contains(Name.ToLower()))
	{
		OutReason = TEXT("Имя запрещено политикой мира");
		return false;
	}
	return true;
}

bool UMMOCharacterEditorSubsystem::CreateCharacter(const FString& TemplateKey, const FString& Name,
	const TMap<FString, FString>& Appearance, FString& OutError)
{
	FString Reason;
	if (!ValidateNameLocal(Name, Reason))
	{
		OutError = Reason; // не отправляем заведомо неверный запрос
		return false;
	}
	if (!Conn) { OutError = TEXT("connection not bound"); return false; }
	return Conn->CreateCharacter(TemplateKey, Name, Appearance, OutError);
}

bool UMMOCharacterEditorSubsystem::EnterWorld(int64 CharacterId, FString& OutError)
{
	if (!Conn) { OutError = TEXT("connection not bound"); return false; }
	return Conn->EnterWorldWithCharacter(CharacterId, OutError);
}

void UMMOCharacterEditorSubsystem::HandleWireJson(uint16 MsgId, const FString& JsonUtf8)
{
	switch (MsgId)
	{
	case MsgCharList:      ParseCharList(JsonUtf8); break;
	case MsgCharTemplates: ParseCharTemplates(JsonUtf8); break;
	case MsgCharCreateOk:  ParseCharCreateOk(JsonUtf8); break;
	case MsgWorldReady:    ParseWorldReady(JsonUtf8); break;
	default: break; // прочие S2C — боевой путь, не редактор
	}
}

static bool ReadJson(const FString& Json, TSharedPtr<FJsonObject>& OutObj)
{
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
}

void UMMOCharacterEditorSubsystem::ParseCharList(const FString& Json)
{
	TSharedPtr<FJsonObject> Obj;
	if (!ReadJson(Json, Obj)) return;

	Roster.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Chars = nullptr;
	if (Obj->TryGetArrayField(TEXT("characters"), Chars))
	{
		for (const TSharedPtr<FJsonValue>& V : *Chars)
		{
			const TSharedPtr<FJsonObject>* C = nullptr;
			if (!V->GetObject(C)) continue;
			FMMOCharRosterEntry E;
			double Cid = 0.0;
			(*C)->TryGetNumberField(TEXT("id"), Cid);
			(*C)->TryGetStringField(TEXT("name"), E.Name);
			(*C)->TryGetStringField(TEXT("race_key"), E.RaceKey);
			(*C)->TryGetStringField(TEXT("class_key"), E.ClassKey);
			double LevelD = 0.0;
				(*C)->TryGetNumberField(TEXT("level"), LevelD);
				E.Level = static_cast<int32>(LevelD);
			E.CharacterId = static_cast<int64>(Cid);
			Roster.Add(MoveTemp(E));
		}
	}
	double MaxCharsD = 5.0;
	Obj->TryGetNumberField(TEXT("max_chars"), MaxCharsD);
	MaxCharsPerAccount = static_cast<int32>(MaxCharsD);
	UE_LOG(LogMMOClient, Log, TEXT("[char-editor] roster: %d chars, max=%d"), Roster.Num(), MaxCharsPerAccount);
	OnCharRosterReady.Broadcast();
}

void UMMOCharacterEditorSubsystem::ParseCharTemplates(const FString& Json)
{
	TSharedPtr<FJsonObject> Obj;
	if (!ReadJson(Json, Obj)) return;

	Templates.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Tpls = nullptr;
	if (!Obj->TryGetArrayField(TEXT("templates"), Tpls)) return;

	for (const TSharedPtr<FJsonValue>& V : *Tpls)
	{
		const TSharedPtr<FJsonObject>* T = nullptr;
		if (!V->GetObject(T)) continue;
		FMMOCharTemplate E;
		(*T)->TryGetStringField(TEXT("template_key"), E.TemplateKey);
		(*T)->TryGetStringField(TEXT("race_key"), E.RaceKey);
		(*T)->TryGetStringField(TEXT("class_key"), E.ClassKey);
		(*T)->TryGetStringField(TEXT("display_name"), E.DisplayName);
		(*T)->TryGetStringField(TEXT("description"), E.Description);
		(*T)->TryGetStringField(TEXT("starting_zone"), E.StartingZone);

		// appearance_schema: {"slots":{slot:[values]},"presets":[...]}
		const TSharedPtr<FJsonObject>* Schema = nullptr;
		if ((*T)->TryGetObjectField(TEXT("appearance_schema"), Schema))
		{
			const TSharedPtr<FJsonObject>* Slots = nullptr;
			if ((*Schema)->TryGetObjectField(TEXT("slots"), Slots))
			{
				for (const auto& SlotKV : (*Slots)->Values)
				{
					TArray<FString> Values;
					const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
					if (SlotKV.Value->TryGetArray(Arr))
					{
						for (const TSharedPtr<FJsonValue>& AV : *Arr)
						{
							FString S;
							if (AV->TryGetString(S)) Values.Add(S);
						}
					}
					E.AppearanceSlots.Add(SlotKV.Key, MoveTemp(Values));
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Presets = nullptr;
			if ((*Schema)->TryGetArrayField(TEXT("presets"), Presets))
			{
				for (const TSharedPtr<FJsonValue>& PV : *Presets)
				{
					FString S;
					if (PV->TryGetString(S)) E.Presets.Add(S);
				}
			}
		}
		Templates.Add(MoveTemp(E));
	}
	UE_LOG(LogMMOClient, Log, TEXT("[char-editor] templates loaded: %d"), Templates.Num());
	OnCharTemplatesReady.Broadcast();
}

void UMMOCharacterEditorSubsystem::ParseCharCreateOk(const FString& Json)
{
	TSharedPtr<FJsonObject> Obj;
	if (!ReadJson(Json, Obj)) return;

	FString Err;
	if (Obj->TryGetStringField(TEXT("error"), Err) && !Err.IsEmpty())
	{
		UE_LOG(LogMMOClient, Warning, TEXT("[char-editor] create failed: %s"), *Err);
		OnCharCreateResult.Broadcast(false, Err);
		return;
	}
	double Cid = 0.0;
	Obj->TryGetNumberField(TEXT("character_id"), Cid);
	LastCreatedCharacterId = static_cast<int64>(Cid);
	FString Name;
	Obj->TryGetStringField(TEXT("name"), Name);
	UE_LOG(LogMMOClient, Log, TEXT("[char-editor] created char #%lld '%s'"), static_cast<int64>(Cid), *Name);
	OnCharCreateResult.Broadcast(true, Name);
}

void UMMOCharacterEditorSubsystem::ParseWorldReady(const FString& Json)
{
	TSharedPtr<FJsonObject> Obj;
	if (!ReadJson(Json, Obj)) return;

	FString Err;
	if (Obj->TryGetStringField(TEXT("error"), Err) && !Err.IsEmpty())
	{
		UE_LOG(LogMMOClient, Warning, TEXT("[char-editor] enter-world failed: %s"), *Err);
		return;
	}

	FMMOWorldReadyInfo Info;
	double Cid = 0.0;
	Obj->TryGetNumberField(TEXT("character_id"), Cid);
	Info.CharacterId = static_cast<int64>(Cid);
	Obj->TryGetStringField(TEXT("name"), Info.Name);
	Obj->TryGetStringField(TEXT("zone_key"), Info.ZoneKey);
	Obj->TryGetStringField(TEXT("resource_type"), Info.ResourceType);
	double HpD = 0.0, MaxHpD = 0.0, ResD = 0.0;
	Obj->TryGetNumberField(TEXT("hp"), HpD);
	Obj->TryGetNumberField(TEXT("max_hp"), MaxHpD);
	Obj->TryGetNumberField(TEXT("resource"), ResD);
	Info.Hp = static_cast<int32>(HpD);
	Info.MaxHp = static_cast<int32>(MaxHpD);
	Info.Resource = static_cast<int32>(ResD);

	const TArray<TSharedPtr<FJsonValue>>* Pos = nullptr;
	if (Obj->TryGetArrayField(TEXT("pos"), Pos) && Pos->Num() >= 3)
	{
		Info.Position = FVector((*Pos)[0]->AsNumber(), (*Pos)[1]->AsNumber(), (*Pos)[2]->AsNumber());
	}
	const TSharedPtr<FJsonObject>* Stats = nullptr;
	if (Obj->TryGetObjectField(TEXT("stats"), Stats))
	{
		for (const auto& KV : (*Stats)->Values)
		{
			Info.Stats.Add(KV.Key, KV.Value->AsNumber());
		}
	}

	UE_LOG(LogMMOClient, Log, TEXT("[char-editor] WORLD_READY char=%lld zone=%s hp=%d/%d res=%s"),
		Info.CharacterId, *Info.ZoneKey, Info.Hp, Info.MaxHp, *Info.ResourceType);
	OnWorldReady.Broadcast(Info);
	// UI-слой: OpenLevel(зона из GameConfig) -> спавн AMMOCharacter на Info.Position
}
