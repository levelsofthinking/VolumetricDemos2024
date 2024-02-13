// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogHoloSuitePlayer, Log, All);
DECLARE_STATS_GROUP(TEXT("HoloSuitePlayer"), STATGROUP_HoloSuitePlayer, STATCAT_Advanced);

class FHoloSuitePlayerModule : public IModuleInterface
{
    public:
	    virtual void StartupModule() override;
	    virtual void ShutdownModule() override;
};
