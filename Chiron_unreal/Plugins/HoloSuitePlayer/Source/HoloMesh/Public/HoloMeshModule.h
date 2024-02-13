// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogHoloMesh, Log, All);
DECLARE_STATS_GROUP(TEXT("HoloMesh"), STATGROUP_HoloMesh, STATCAT_Advanced);

class FHoloMeshModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
