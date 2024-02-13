// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloSuitePlayerModule.h"
#include "CoreMinimal.h"
#include "ShaderCore.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FHoloSuitePlayerModule"
DEFINE_LOG_CATEGORY(LogHoloSuitePlayer);

void FHoloSuitePlayerModule::StartupModule()
{
	FString BaseDir = IPluginManager::Get().FindPlugin("HoloSuitePlayer")->GetBaseDir();

	const FString ShaderDir = FPaths::Combine(BaseDir, TEXT("/Shaders"));
	if (FPaths::DirectoryExists(ShaderDir))
	{
		AddShaderSourceDirectoryMapping(FString("/HoloSuitePlayer"), ShaderDir);
	}
}

void FHoloSuitePlayerModule::ShutdownModule()
{

}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHoloSuitePlayerModule, HoloSuitePlayer)