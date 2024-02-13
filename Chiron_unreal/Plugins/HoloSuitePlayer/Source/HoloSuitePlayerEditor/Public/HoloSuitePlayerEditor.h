// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"

class FHoloSuitePlayerEditorStyle;

DECLARE_LOG_CATEGORY_EXTERN(LogHoloSuitePlayerEditor, Log, All);

class FHoloSuitePlayerEditorModule : public IModuleInterface
{
    public:
	    virtual void StartupModule() override;
	    virtual void ShutdownModule() override;

    private:
        TSharedPtr<FHoloSuitePlayerEditorStyle> StyleSet;
        FDelegateHandle HoloSuiteTrackCreateEditorHandle;

    protected:
        void RegisterEditorDelegates();
        void UnregisterEditorDelegates();

        void HandleEditorBeginPIE(bool bIsSimulating);
        void HandleEditorEndPIE(bool bIsSimulating);
        void HandleEditorPausePIE(bool bIsSimulating);
        void HandleEditorResumePIE(bool bIsSimulating);

};
