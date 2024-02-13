// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloMeshModule.h"

#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "GlobalShader.h"
#include "Interfaces/IPluginManager.h"

IMPLEMENT_MODULE(FHoloMeshModule, HoloMesh)
DEFINE_LOG_CATEGORY(LogHoloMesh);

void FHoloMeshModule::StartupModule()
{
	FString BaseDir = IPluginManager::Get().FindPlugin("HoloSuitePlayer")->GetBaseDir();

	FString ShaderDir = FPaths::Combine(BaseDir, TEXT("/Shaders/HoloMesh"));
	if (FPaths::DirectoryExists(ShaderDir))
	{
		AddShaderSourceDirectoryMapping(FString("/HoloMesh"), ShaderDir);
	}
}

void FHoloMeshModule::ShutdownModule()
{
	
}

