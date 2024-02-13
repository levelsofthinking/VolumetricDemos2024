// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HoloMesh : ModuleRules
{
	public HoloMesh(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] { });

		PrivateIncludePaths.AddRange(
			new string[] {
					ModuleDirectory + "/Private",
					EngineDirectory + "/Source/Runtime/Renderer/Private",
					EngineDirectory + "/Shaders/Shared",
			}
		);

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// All the modules we want as string
			"Core",
			"CoreUObject",
			"Engine",
			"Renderer",
			"RenderCore",
			"RHI",
			"Projects",
			"PhysicsCore"
		});
	}
}
