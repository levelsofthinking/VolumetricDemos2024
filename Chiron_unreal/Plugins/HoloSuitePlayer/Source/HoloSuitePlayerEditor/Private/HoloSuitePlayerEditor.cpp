// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloSuitePlayerEditor.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ISettingsModule.h"
#include "ISequencerModule.h"

#include "HoloSuitePlayerSettings.h"
#include "HoloSuitePlayerEditorStyle.h"
#include "HoloMeshManager.h"
#include "AVV/AVVFileActions.h"
#include "OMS/OMSFileActions.h"
#include "Sequencer/HoloSuiteTrackEditor.h"

#define LOCTEXT_NAMESPACE "FHoloSuitePlayerEditorModule"
DEFINE_LOG_CATEGORY(LogHoloSuitePlayerEditor);

void FHoloSuitePlayerEditorModule::StartupModule()
{
    StyleSet = FHoloSuitePlayerEditorStyle::Get();

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    AssetTools.RegisterAssetTypeActions(MakeShareable(new FOMSFileActions()));
    AssetTools.RegisterAssetTypeActions(MakeShareable(new FAVVFileActions()));

    RegisterEditorDelegates();

    // Register Sequencer Track
    ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
    HoloSuiteTrackCreateEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FHoloSuiteTrackEditor::CreateTrackEditor));

    if (ISettingsModule* SettingModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    {
        SettingModule->RegisterSettings("Project", "Plugins", "HoloSuitePlayer",
            LOCTEXT("RuntimeSettingsName", "HoloSuite Player"),
            LOCTEXT("RuntimeSettingsDescription", "Configure settings for HoloSuite Player plugin."),
            GetMutableDefault<UHoloSuitePlayerSettings>()
        );
    }
}

void FHoloSuitePlayerEditorModule::ShutdownModule()
{
    // Unregister Sequencer Track
    ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer");
    if (SequencerModule)
    {
        SequencerModule->UnRegisterTrackEditor(HoloSuiteTrackCreateEditorHandle);
    }

    UnregisterEditorDelegates();

    StyleSet = nullptr;
}

/** Register Editor delegates. */
void FHoloSuitePlayerEditorModule::RegisterEditorDelegates()
{
    FEditorDelegates::BeginPIE.AddRaw(this, &FHoloSuitePlayerEditorModule::HandleEditorBeginPIE);
    FEditorDelegates::EndPIE.AddRaw(this, &FHoloSuitePlayerEditorModule::HandleEditorEndPIE);
    FEditorDelegates::PausePIE.AddRaw(this, &FHoloSuitePlayerEditorModule::HandleEditorPausePIE);
    FEditorDelegates::ResumePIE.AddRaw(this, &FHoloSuitePlayerEditorModule::HandleEditorResumePIE);
}

/** Unregister Editor delegates. */
void FHoloSuitePlayerEditorModule::UnregisterEditorDelegates()
{
    FEditorDelegates::BeginPIE.RemoveAll(this);
    FEditorDelegates::EndPIE.RemoveAll(this);
    FEditorDelegates::PausePIE.RemoveAll(this);
    FEditorDelegates::ResumePIE.RemoveAll(this);
}

void FHoloSuitePlayerEditorModule::HandleEditorBeginPIE(bool bIsSimulating)
{
    GHoloMeshManager.BeginPIE();
}

void FHoloSuitePlayerEditorModule::HandleEditorEndPIE(bool bIsSimulating)
{
    GHoloMeshManager.EndPIE();
}

void FHoloSuitePlayerEditorModule::HandleEditorPausePIE(bool bIsSimulating)
{

}

void FHoloSuitePlayerEditorModule::HandleEditorResumePIE(bool bIsSimulating)
{

}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHoloSuitePlayerEditorModule, HoloSuitePlayerEditor)
