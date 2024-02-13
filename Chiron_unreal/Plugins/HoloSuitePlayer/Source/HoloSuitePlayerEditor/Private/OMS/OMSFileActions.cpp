// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "OMS/OMSFileActions.h"
#include "OMS/OMSSkeletalMeshFactory.h"
#include "OMS/OMSFile.h"
#include "OMS/oms.h"
#include "HoloSuitePlayerEditor.h"

#include "Engine/Texture.h"
#include "ToolMenus.h"
#include "Misc/PackageName.h"
#include "Misc/PathViews.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "EditorStyleSet.h"
#include "Materials/Material.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/SkeletonFactory.h"
#include "Runtime/CoreUObject/Public/UObject/ConstructorHelpers.h"
#include "PackageHelperFunctions.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 0
#include "AssetRegistry/AssetRegistryModule.h"
#else
#include "AssetRegistryModule.h"
#endif

#if ENGINE_MAJOR_VERSION == 5
#include "UObject/SavePackage.h"
#endif

#define LOCTEXT_NAMESPACE "AssetTypeActions"

FOMSFileActions::FOMSFileActions()
{
}

/* FAssetTypeActions_Base overrides
 *****************************************************************************/

bool FOMSFileActions::CanFilter()
{
    return true;
}

void FOMSFileActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
    auto OMSFiles = GetTypedWeakObjectPtrs<UOMSFile>(InObjects);

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 0
    FName StyleName = FAppStyle::GetAppStyleSetName();
#else
    FName StyleName = FEditorStyle::GetStyleSetName();
#endif

    Section.AddMenuEntry(
        "OMSFile_CreateLitMaterial",
        LOCTEXT("OMSFile_CreateLitMaterial", "Create Lit Material"),
        LOCTEXT("OMSFile_CreateLitMaterialTooltip", "Creates a lit material for volumetric playback."),
        FSlateIcon(StyleName, "ClassIcon.Material"),
        FUIAction(
            FExecuteAction::CreateSP(this, &FOMSFileActions::ExecuteCreateLitMaterial, OMSFiles),
            FCanExecuteAction()
        )
    );

    Section.AddMenuEntry(
        "OMSFile_CreateUnlitMaterial",
        LOCTEXT("OMSFile_CreateUnlitMaterial", "Create Unlit Material"),
        LOCTEXT("OMSFile_CreateUnlitMaterialTooltip", "Creates an unlit material for volumetric playback."),
        FSlateIcon(StyleName, "ClassIcon.Material"),
        FUIAction(
            FExecuteAction::CreateSP(this, &FOMSFileActions::ExecuteCreateUnlitMaterial, OMSFiles),
            FCanExecuteAction()
        )
    );

    Section.AddMenuEntry(
        "OMSFile_CreateActorAttachmentSkeletalMesh",
        LOCTEXT("OMSFile_CreateActorAttachmentSkeletalMesh", "Create Actor Attachment SkeletalMesh"),
        LOCTEXT("OMSFile_CreateActorAttachmentSkeletalMeshTooltip", "Creates a SkeletalMesh to attach actors to."),
        FSlateIcon(StyleName, "ClassIcon.Material"),
        FUIAction(
            FExecuteAction::CreateSP(this, &FOMSFileActions::ExecuteCreateActorAttachmentSkeleton, OMSFiles),
            FCanExecuteAction()
        )
    );

    Section.AddMenuEntry(
        "OMSFile_CreateRetargetSkeletalMesh",
        LOCTEXT("OMSFile_CreateRetargetSkeletalMesh", "Create Retarget SkeletalMesh"),
        LOCTEXT("OMSFile_CreateRetargetSkeletalMeshTooltip", "Creates a SkeletalMesh for retargeting."),
        FSlateIcon(StyleName, "ClassIcon.Material"),
        FUIAction(
            FExecuteAction::CreateSP(this, &FOMSFileActions::ExecuteCreateRetargetSkeleton, OMSFiles),
            FCanExecuteAction()
        )
    );
}

uint32 FOMSFileActions::GetCategories()
{
#if ENGINE_MAJOR_VERSION == 4
    return EAssetTypeCategories::MaterialsAndTextures | EAssetTypeCategories::Media;
#else
    return EAssetTypeCategories::Materials | EAssetTypeCategories::Media;
#endif
}


FText FOMSFileActions::GetName() const
{
    return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_OMSFile", "OMS");
}

UClass* FOMSFileActions::GetSupportedClass() const
{
    return UOMSFile::StaticClass();
}


FColor FOMSFileActions::GetTypeColor() const
{
    return FColor::Green;
}

bool FOMSFileActions::HasActions(const TArray<UObject*>& InObjects) const
{
    return true;
}

bool FOMSFileActions::IsImportedAsset() const
{
    return true;
}

void FOMSFileActions::GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const
{
    for (auto& Asset : TypeAssets)
    {
        const auto OMSFile = CastChecked<UOMSFile>(Asset);
        if (OMSFile)
        {
            OutSourceFilePaths.Add(OMSFile->GetPath());
        }
    }
}

void FOMSFileActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
    
}

/* FAssetTypeActions_OMSFile internal functions
 *****************************************************************************/

void CreateMaterial(UOMSFile* Object, FString DefaultSuffix, FString Name, FString PackagePath, FString MaterialPath)
{
    // Create package
#if ENGINE_MAJOR_VERSION == 5 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
    UPackage* NewPackage = CreatePackage(*PackagePath);
#else
    UPackage* NewPackage = CreatePackage(nullptr, *PackagePath);
#endif

    // Create material
    UMaterial* sourceMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), NULL, *MaterialPath, NULL, LOAD_NoWarn, NULL));

    if (sourceMaterial != nullptr)
    {
        UObject* newMaterial = StaticDuplicateObject(sourceMaterial, NewPackage, *Name);

#if ENGINE_MAJOR_VERSION == 5
        FSavePackageArgs args;
        args.TopLevelFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Standalone;
        args.Error = GError;
        args.bForceByteSwapping = true;
        args.bWarnOfLongFilename = true;
        args.SaveFlags = SAVE_NoError;
        bool bSaved = UPackage::SavePackage(NewPackage, nullptr, *FPackageName::GetLongPackagePath(PackagePath), args);
#else
        bool bSaved = UPackage::SavePackage(NewPackage, nullptr, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, *FPackageName::GetLongPackagePath(PackagePath), GError, nullptr, true, true, SAVE_NoError);
#endif

        // Mark the package dirty
        NewPackage->MarkPackageDirty();

        // Notify the asset registry
        FAssetRegistryModule::AssetCreated(newMaterial);
    }
}

/* FAssetTypeActions_OMSFile callbacks
 *****************************************************************************/

void FOMSFileActions::ExecuteCreateLitMaterial(TArray<TWeakObjectPtr<UOMSFile>> Objects)
{
    IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
    const FString DefaultSuffix = TEXT("_LitMaterial");
    FString MaterialPath = TEXT("Material'/HoloSuitePlayer/Arcturus/HoloSuite_OMSLit_Mat.HoloSuite_OMSLit_Mat'");

    if (Objects.Num() == 1)
    {
        auto Object = Objects[0].Get();

        if (Object != nullptr)
        {
            // Determine an appropriate name
            FString Name;
            FString PackagePath;
            CreateUniqueAssetName(Object->GetOutermost()->GetName(), DefaultSuffix, PackagePath, Name);

            // Create the material
            CreateMaterial(Object, DefaultSuffix, Name, PackagePath, MaterialPath);
        }
    }
}

void FOMSFileActions::ExecuteCreateUnlitMaterial(TArray<TWeakObjectPtr<UOMSFile>> Objects)
{
    IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
    const FString DefaultSuffix = TEXT("_UnlitMaterial");
    FString MaterialPath = TEXT("Material'/HoloSuitePlayer/Arcturus/HoloSuite_OMSUnlit_Mat.HoloSuite_OMSUnlit_Mat'");

    if (Objects.Num() == 1)
    {
        auto Object = Objects[0].Get();

        if (Object != nullptr)
        {
            // Determine an appropriate name
            FString Name;
            FString PackagePath;
            CreateUniqueAssetName(Object->GetOutermost()->GetName(), DefaultSuffix, PackagePath, Name);

            // Create the material
            CreateMaterial(Object, DefaultSuffix, Name, PackagePath, MaterialPath);
        }
    }
}

void FOMSFileActions::ExecuteCreateActorAttachmentSkeleton(TArray<TWeakObjectPtr<UOMSFile>> Objects)
{
    IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
    const FString DefaultSuffix = TEXT("_SkeletalMesh");

    if (Objects.Num() == 1)
    {
        auto Object = Objects[0].Get();

        if (Object != nullptr)
        {
            oms_header_t header = {};
            FStreamableOMSData& OMSStreamableData = (FStreamableOMSData&)Object->GetStreamableData();
            OMSStreamableData.ReadHeaderSync(&header);

            if (!header.has_retarget_data)
            {
                FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("No Skeleton Data", "The selected OMS contains no skeletal data and a SkeletalMesh cannot be generated."));
                return;
            }

            // Determine an appropriate name
            FString Name;
            FString PackagePath;
            CreateUniqueAssetName(Object->GetOutermost()->GetName(), DefaultSuffix, PackagePath, Name);

            // Create the skeletal mesh
            UOMSSkeletalMeshFactory* SkeletalMeshFactory = NewObject<UOMSSkeletalMeshFactory>();
            SkeletalMeshFactory->SourceOMS = Object;
            SkeletalMeshFactory->Retargeting = false;
            ContentBrowserSingleton.CreateNewAsset(Name, FPackageName::GetLongPackagePath(PackagePath), USkeletalMesh::StaticClass(), SkeletalMeshFactory);
        }
    }
}

void FOMSFileActions::ExecuteCreateRetargetSkeleton(TArray<TWeakObjectPtr<UOMSFile>> Objects)
{
    IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
    const FString DefaultSuffix = TEXT("_SkeletalMesh");

    if (Objects.Num() == 1)
    {
        auto Object = Objects[0].Get();

        if (Object != nullptr)
        {
            oms_header_t header = {};
            FStreamableOMSData& OMSStreamableData = (FStreamableOMSData&)Object->GetStreamableData();
            OMSStreamableData.ReadHeaderSync(&header);

            if (!header.has_retarget_data)
            {
                FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("No Retarget Data", "The selected OMS contains no retargeting data and a SkeletalMesh cannot be generated."));
                return;
            }

            // Determine an appropriate name
            FString Name;
            FString PackagePath;
            CreateUniqueAssetName(Object->GetOutermost()->GetName(), DefaultSuffix, PackagePath, Name);

            // Create the skeletal mesh
            UOMSSkeletalMeshFactory* SkeletalMeshFactory = NewObject<UOMSSkeletalMeshFactory>();
            SkeletalMeshFactory->SourceOMS = Object;
            ContentBrowserSingleton.CreateNewAsset(Name, FPackageName::GetLongPackagePath(PackagePath), USkeletalMesh::StaticClass(), SkeletalMeshFactory);
        }
    }
}

#undef LOCTEXT_NAMESPACE
