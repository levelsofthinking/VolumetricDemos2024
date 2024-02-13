// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class HoloSuitePlayer : ModuleRules
{
	public HoloSuitePlayer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(
			new string[] {
				ModuleDirectory + "/Private",
				EngineDirectory + "/Source/Runtime/Renderer/Private",
				EngineDirectory + "/Shaders/Shared",
			}
			);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"HoloMesh",
				"Projects",
				"RenderCore",
				"Renderer",
				"RHI",
				"MediaAssets",
                "MediaUtils",
                "SlateCore",
                "AudioMixer",
                "SignalProcessing",
                "MovieScene"
            }
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// ... add private dependencies that you statically link with here ...
			}
			);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
    }
}
