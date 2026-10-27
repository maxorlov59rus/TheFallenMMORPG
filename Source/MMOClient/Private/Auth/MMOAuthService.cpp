// Copyright MMOClient Project. All Rights Reserved.

#include "Auth/MMOAuthService.h"
#include "MMOClient.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UMMOAuthService::Login(const FString& Username, const FString& Password)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(AuthEndpoint);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("username"), Username);
	Body->SetStringField(TEXT("password"), Password);

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);
	Request->SetContentAsString(Out);

	Request->OnProcessRequestComplete().BindUObject(this, &UMMOAuthService::OnLoginResponse);
	Request->ProcessRequest();

	UE_LOG(LogMMOAuth, Log, TEXT("Login request sent for user '%s'"), *Username);
}

void UMMOAuthService::RequestSessionResume()
{
	if (CachedToken.IsEmpty())
	{
		OnLoginFinished.Broadcast(false, TEXT("No cached session token"));
		return;
	}
	OnLoginFinished.Broadcast(true, CachedToken);
}

void UMMOAuthService::OnLoginResponse(FHttpResult* Result)
{
	if (!Result || !Result->GetSuccess() || Result->GetResponseCode() != EHttpResponseCodes::Ok)
	{
		const int32 Code = Result ? Result->GetResponseCode() : 0;
		UE_LOG(LogMMOAuth, Warning, TEXT("Login failed, HTTP code %d"), Code);
		OnLoginFinished.Broadcast(false, TEXT("AUTH_NETWORK_ERROR"));
		return;
	}

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		OnLoginFinished.Broadcast(false, TEXT("AUTH_BAD_RESPONSE"));
		return;
	}

	FString Token;
	if (!Json->TryGetStringField(TEXT("token"), Token))
	{
		OnLoginFinished.Broadcast(false, TEXT("AUTH_NO_TOKEN"));
		return;
	}

	CachedToken = Token; // TODO: хранить в зашифрованном keychain/secure storage
	UE_LOG(LogMMOAuth, Log, TEXT("Login OK, token received"));
	OnLoginFinished.Broadcast(true, Token);
}
