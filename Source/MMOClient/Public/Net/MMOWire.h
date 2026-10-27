// Copyright MMOClient Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/IPaddr.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/Socket.h"

class UMMOConnectionManager;

/**
 * ogni_wire фрейминг — зеркало Server/tools/ogni_wire.py и Docs/PROTOCOL.md.
 * Заголовок 10 байт: magic 'OF'(2) + ver(1) + msg_id(2 LE) + seq(4 LE) + flags(1).
 * UDP-сокет поверх кастомного reliable-слоя (handshake сейчас — request/reply с ретраями).
 */
namespace OgnWire
{
	inline constexpr uint8 Magic0 = 'O';
	inline constexpr uint8 Magic1 = 'F';
	inline constexpr uint8 Version = 1;
	inline constexpr int32 HeaderLen = 10;

	enum class EMsgId : uint16
	{
		MoveUpdate       = 0x1001,
		SwingBegin       = 0x1002,
		CastAbility      = 0x1003,
		ForgeCraft       = 0x1004,
		CollectSoulDrop  = 0x1005,
		ClanPing         = 0x1006,
		SessionAccept    = 0x2F00,
		ProtocolMismatch = 0x2F01,
		SessionHello     = 0x2F02,
		SpawnEntity      = 0x2001,
		DespawnEntity    = 0x2002,
		SwingResult      = 0x2010,
		AbilityAck       = 0x2011,
		WorldState       = 0x2020,
		DeathLastChance  = 0x2021,
		SoulDelta        = 0x2030,
		ForgeMenu        = 0x2031,
		RepChange        = 0x2040,
		// персонажная линия (редактор создания) — зеркало ogni_wire.py / msg_registry
		CharListReq      = 0x3001,
		CharList         = 0x3002,
		CharTemplates    = 0x3003,   // BOTH: C2S-запрос => S2C-ответ с тем же id
		CharCreate       = 0x3010,
		CharCreateOk     = 0x3011,
		CharEnterWorld   = 0x3012,
		WorldReady       = 0x3013,
	};

	enum EFlags : uint8
	{
		Ack        = 0x01,
		Reliable   = 0x02,
		Compressed = 0x04,
	};

	struct FFrame
	{
		uint8  Version = 0;
		EMsgId MsgId = EMsgId::SessionHello;
		uint32 Seq = 0;
		uint8  Flags = 0;
		TArray<uint8> Body;

		/** false при битом magic/version/размере — пакет дропается без ответа */
		static bool Parse(const uint8* Data, int32 Len, FFrame& Out);
	};

	/** Сериализация C2S_SessionHello: u16 len(jwt utf8) + jwt bytes (зеркало worldserver_ref.handle_frame) */
	void MakeSessionHello(const FString& Jwt, uint32 Seq, TArray<uint8>& OutFrame);

	/** Разбор S2C_SessionAccept: u16 keylen + session_key(32) + u32 aoi_seed */
	bool ParseSessionAccept(const TArray<uint8>& Body, TArray<uint8>& OutSessionKey, uint32& OutAoiSeed);

	/** JSON-кадры персонажной линии (payload = UTF-8 JSON, зеркало handle_char_message в worldserver_ref.py) */
	void MakeJsonFrame(EMsgId MsgId, uint32 Seq, const FString& JsonUtf8, TArray<uint8>& OutFrame);
	bool ParseJsonBody(const TArray<uint8>& Body, FString& OutJsonUtf8);
}

/**
 * Тонкий UDP-транспорт handshake'а. Отделён от UNetDriver/Iris:
 * боевой путь Iris (вход в мир через PlayerController) остаётся за ConnectionManager,
 * а авторизационный ogni-wire handshake идёт по выделенному сокету на world_port.
 */
class FMMOWireLink
{
public:
	explicit FMMOWireLink(UMMOConnectionManager* InOwner);
	~FMMOWireLink();

	bool Open(const FString& Host, uint32 Port, FString& OutError);
	void SendHello(const FString& Jwt);
	/** опрос сокета; вызывать из Tick owner-объекта. bTimedOut — для ретрая снаружи */
	void Poll(bool& OutAccepted, bool& OutRejected, bool& OutTimedOut);
	void Close();

	/** Персонажная линия (редактор): JSON-кадры поверх established handshake-сокета.
	 *  Ответы приходят через SetJsonHandler (msg_id = CharList/CharTemplates/CharCreateOk/WorldReady). */
	bool SendJson(OgnWire::EMsgId MsgId, const FString& JsonUtf8, FString& OutError);
	using FOnWireJsonNative = TFunction<void(uint16 /*EMsgId*/, const FString& /*Json*/)>;
	void SetJsonHandler(FOnWireJsonNative InHandler) { OnWireJsonNative = MoveTemp(InHandler); }

	const TArray<uint8>& LastSessionKey() const { return SessionKey; }
	uint32 LastAoiSeed() const { return AoiSeed; }
	bool IsOpen() const { return Socket != nullptr; }

private:
	FDelegateHandle ReadHandle;
	FSocket* Socket = nullptr;
	FOnWireJsonNative OnWireJsonNative;   // dispatch S2C JSON-кадров персонажной линии
	uint32 OutSeq = 1;                    // seq для C2S-кадров после handshake
	TWeakObjectPtr<UMMOConnectionManager> Owner;
	uint32 HelloSeq = 1;
	double HelloSentTime = 0.0;
	static constexpr double HandshakeTimeoutSec = 3.0;
	static constexpr int32 MaxHandshakeRetries = 3;
	int32 RetriesLeft = MaxHandshakeRetries;
	TArray<uint8> PendingFrame;
	TSharedPtr<FInternetAddr> ServerAddr;
	TArray<uint8> SessionKey;
	uint32 AoiSeed = 0;
};