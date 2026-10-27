// Copyright MMOClient Project. All Rights Reserved.

using UnrealBuildTool;

public class MMOClient : ModuleRules
{
	public MMOClient(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"NetCore",
					"Sockets",
			"OnlineSubsystem",
			"OnlineSubsystemUtils"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UMG",
			"HTTP",
			"Json",
			"JsonUtilities",
			"Niagara"
		});

		// Для Shipping-сборки клиента редакторские модули отключены
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
