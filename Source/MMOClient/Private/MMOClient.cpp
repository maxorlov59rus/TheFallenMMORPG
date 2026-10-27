// Copyright MMOClient Project. All Rights Reserved.

#include "MMOClient.h"

DEFINE_LOG_CATEGORY(LogMMOClient);
DEFINE_LOG_CATEGORY(LogMMONet);
DEFINE_LOG_CATEGORY(LogMMOAuth);

#define LOCTEXT_NAMESPACE "FMMOClientModule"

class FMMOClientModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogMMOClient, Log, TEXT("MMOClient module started"));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogMMOClient, Log, TEXT("MMOClient module shut down"));
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_PRIMARY_GAME_MODULE(FMMOClientModule, MMOClient, "MMOClient");
