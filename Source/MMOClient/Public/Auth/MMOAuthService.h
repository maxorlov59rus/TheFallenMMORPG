// Copyright MMOClient Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Http.h"
#include "MMOAuthService.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoginResult,
	bool, bSuccess, const FString&, TokenOrError);

/**
 * Клиентский сервис аутентификации: логин по login/password через HTTPS,
 * получение JWT-токена для handshake с игровым сервером.
 * Пароли никогда не отправляются на game-сервер напрямую.
 */
UCLASS(BlueprintType)
class MMOCLIENT_API UMMOAuthService : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Config, Category = "MMO|Auth")
	FString AuthEndpoint = TEXT("https://auth.example.com/api/v1/login");

	UFUNCTION(BlueprintCallable, Category = "MMO|Auth")
	void Login(const FString& Username, const FString& Password);

	UFUNCTION(BlueprintCallable, Category = "MMO|Auth")
	void RequestSessionResume();

	UPROPERTY(BlueprintAssignable, Category = "MMO|Auth")
	FOnLoginResult OnLoginFinished;

private:
	void OnLoginResponse(FHttpResult* Result);

	FString CachedToken;
};
