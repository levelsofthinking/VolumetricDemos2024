// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HoloSuitePlayerEditor : ModuleRules
{
	public HoloSuitePlayerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);

		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"HoloSuitePlayer",
				"HoloMesh",
				"Projects",
				"RenderCore",
				"RHI",
				"MediaAssets",
                "SlateCore",
                "UnrealEd",
                "ToolMenus",
                "Slate",
                "EditorStyle",
                "Sequencer",
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
