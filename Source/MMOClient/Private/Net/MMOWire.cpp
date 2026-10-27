// Copyright MMOClient Project. All Rights Reserved.

#include "Net/MMOWire.h"
#include "MMOClient.h"


// ----------little-endian helpers (wire = LE, см. ogni_wire.py struct '<...')----------
static void AppendU16(TArray<uint8>& A, uint16 V) { A.Append(reinterpret_cast<uint8*>(&V), 2); } // UE всегда LE-таргеты
static void AppendU32(TArray<uint8>& A, uint32 V) { A.Append(reinterpret_cast<uint8*>(&V), 4); }

namespace OgnWire
{
	bool FFrame::Parse(const uint8* Data, int32 Len, FFrame& Out)
	{
		if (Len < HeaderLen || Data[0] != Magic0 || Data[1] != Magic1)
		{
			return false; // мусор/спуфинг — дроп без ответа (политика worldserver_ref)
		}
		Out.Version = Data[2];
		FMemory::Memcpy(&Out.MsgId,  Data + 3, 2);
		FMemory::Memcpy(&Out.Seq,    Data + 5, 4);
		Out.Flags   = Data[9];
		Out.Body.Append(Data + HeaderLen, Len - HeaderLen);
		return Out.Version == Version;
	}

	void MakeSessionHello(const FString& Jwt, uint32 Seq, TArray<uint8>& OutFrame)
	{
		FTCHARToUTF8 Utf8(*Jwt);
		const int32 JwtLen = Utf8.Length();
		check(JwtLen <= 0xFFFF);

		OutFrame.Reset(HeaderLen + 2 + JwtLen);
		OutFrame.Add(Magic0);
		OutFrame.Add(Magic1);
		OutFrame.Add(Version);
		AppendU16(OutFrame, static_cast<uint16>(EMsgId::SessionHello));
		AppendU32(OutFrame, Seq);
		OutFrame.Add(Reliable);
		AppendU16(OutFrame, static_cast<uint16>(JwtLen));
		OutFrame.Append(reinterpret_cast<const uint8*>(Utf8.Get()), JwtLen);
	}

	bool ParseSessionAccept(const TArray<uint8>& Body, TArray<uint8>& OutSessionKey, uint32& OutAoiSeed)
	{
		if (Body.Num() < 2 + 32 + 4) return false;
		uint16 KeyLen; FMemory::Memcpy(&KeyLen, Body.GetData(), 2);
		if (KeyLen != 32 || Body.Num() < 2 + KeyLen + 4) return false;
		OutSessionKey.Append(Body.GetData() + 2, KeyLen);
		FMemory::Memcpy(&OutAoiSeed, Body.GetData() + 2 + KeyLen, 4);
		return true;
	}

	void MakeJsonFrame(EMsgId MsgId, uint32 Seq, const FString& JsonUtf8, TArray<uint8>& OutFrame)
	{
		FTCHARToUTF8 Utf8(*JsonUtf8);
		OutFrame.Reset(HeaderLen + Utf8.Length());
		OutFrame.Add(Magic0);
		OutFrame.Add(Magic1);
		OutFrame.Add(Version);
		AppendU16(OutFrame, static_cast<uint16>(MsgId));
		AppendU32(OutFrame, Seq);
		OutFrame.Add(Reliable);
		OutFrame.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}

	bool ParseJsonBody(const TArray<uint8>& Body, FString& OutJsonUtf8)
	{
		// JSON-кадры персонажной линии: тело = UTF-8 JSON целиком (handle_char_message в worldserver_ref.py)
		if (Body.Num() < 2 || Body[0] != '{') return false;
		OutJsonUtf8 = FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Body.GetData())));
		return true;
	}
}

// ----------FMMOWireLink----------
FMMOWireLink::FMMOWireLink(UMMOConnectionManager* InOwner)
	: Owner(InOwner)
{
}

FMMOWireLink::~FMMOWireLink()
{
	Close();
}

bool FMMOWireLink::Open(const FString& Host, uint32 Port, FString& OutError)
{
	Close();

	int32 Err = 0;
	ISocketSubsystem* SS = ISocketSubsystem::Get(SOCKETPLATFORM_SUBSYSTEM);
	if (!SS) { OutError = TEXT("no socket subsystem"); return false; }
	Socket = SS->CreateSocket(NAME_SOCKET_PROTOCOL_UDP, TEXT("OgniWireHandshake"), /*bIsServerSocketThreadBased=*/false);
	if (!Socket)
	{
		OutError = TEXT("socket alloc failed");
		return false;
	}

	TSharedRef<FInternetAddr> Addr = SS->CreateInternetAddr();
	Addr->SetIp(*Host, Err);
	Addr->SetPort(static_cast<int32>(Port));
	if (Err != SE_NO_ERROR)
	{
		OutError = FString::Printf(TEXT("bad server address %s:%u"), *Host, Port);
		Close();
		return false;
	}

	ServerAddr = Addr.Get();
	if (!Socket->Connect(Addr.Get()))
	{
		OutError = FString::Printf(TEXT("udp connect to %s:%u failed"), *Host, Port);
		Close();
		return false;
	}
	Socket->SetNonBlocking(true);
	RetriesLeft = MaxHandshakeRetries;
	HelloSeq = 1;
	return true;
}

void FMMOWireLink::SendHello(const FString& Jwt)
{
	if (!Socket) return;
	OgnWire::MakeSessionHello(Jwt, HelloSeq, PendingFrame);
	int32 Sent = 0;
	Socket->SendTo(ServerAddr.Get(), PendingFrame.GetData(), PendingFrame.Num(), Sent);
	HelloSentTime = FPlatformTime::Seconds();
	UE_LOG(LogMMONet, Log, TEXT("[wire] C2S_SessionHello sent (jwt %d chars, seq %u)"), Jwt.Len(), HelloSeq);
}

bool FMMOWireLink::SendJson(OgnWire::EMsgId MsgId, const FString& JsonUtf8, FString& OutError)
{
	if (!Socket) { OutError = TEXT("wire link not open"); return false; }
	TArray<uint8> Frame;
	OgnWire::MakeJsonFrame(MsgId, OutSeq, JsonUtf8, Frame);
	int32 Sent = 0;
	const bool bOk = Socket->SendTo(ServerAddr.Get(), Frame.GetData(), Frame.Num(), Sent) && Sent == Frame.Num();
	if (bOk)
	{
		++OutSeq;
		UE_LOG(LogMMONet, VeryVerbose, TEXT("[wire] JSON C2S msg=%d seq=%u (%d bytes)"),
			static_cast<int32>(MsgId), OutSeq - 1, Frame.Num());
	}
	else
	{
		OutError = TEXT("udp send failed");
	}
	return bOk;
}

void FMMOWireLink::Poll(bool& OutAccepted, bool& OutRejected, bool& OutTimedOut)
{
	OutAccepted = OutRejected = OutTimedOut = false;
	if (!Socket) return;

	uint8 Buf[2048];
	int32 ReadSize = 0;
	ISocketSubsystem* SS = ISocketSubsystem::Get(SOCKETPLATFORM_SUBSYSTEM);
	TSharedRef<FInternetAddr> From = SS->CreateInternetAddr();
	bool bOk = false;
	while (Socket->HasPendingConnection(bOk) && bOk &&
	       Socket->RecvFrom(From.Get(), Buf, sizeof(Buf), ReadSize, ESocketReceiveFlags::None))
	{
		if (ReadSize <= 0) continue;
		OgnWire::FFrame Fr;
		if (!OgnWire::FFrame::Parse(Buf, ReadSize, Fr)) continue;
		if (Fr.MsgId == OgnWire::EMsgId::SessionAccept)
		{
			if (OgnWire::ParseSessionAccept(Fr.Body, SessionKey, AoiSeed))
			{
				OutAccepted = true;
				return;
			}
		}
		else if (Fr.MsgId == OgnWire::EMsgId::ProtocolMismatch)
		{
			OutRejected = true; // bad jwt / revoked / build mismatch — ретрай бессмысленен
			return;
		}
		else if (OnWireJsonNative.IsBound())
		{
			// персонажная линия и прочие S2C JSON-кадры (CharList/CharTemplates/CharCreateOk/WorldReady...)
			FString JsonStr;
			if (OgnWire::ParseJsonBody(Fr.Body, JsonStr))
			{
				OnWireJsonNative.Execute(static_cast<uint16>(Fr.MsgId), JsonStr);
			}
		}
	}

	if (!OutAccepted && !OutRejected && HelloSentTime > 0.0 &&
	    FPlatformTime::Seconds() - HelloSentTime > HandshakeTimeoutSec)
	{
		if (--RetriesLeft <= 0) { OutRejected = true; return; }
		++HelloSeq;
		OutTimedOut = true; // owner перешлёт hello
	}
}

void FMMOWireLink::Close()
{
	if (Socket)
	{
		if (ISocketSubsystem* SS = ISocketSubsystem::Get(SOCKETPLATFORM_SUBSYSTEM))
			SS->DestroySocket(Socket);
		Socket = nullptr;
	}
	ServerAddr.Reset();
}