// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "AVV/AVVFileActions.h"
#include "AVV/AVVDecoder.h"
#include "AVV/AVVDecoderCPU.h"
#include "AVV/AVVDecoderCompute.h"
#include "AVV/AVVFile.h"
#include "AVV/AVVFormat.h"
#include "AVV/AVVSkeletalMeshFactory.h"

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

#if ENGINE_MAJOR_VERSION == 5
#include "UObject/SavePackage.h"
#endif

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 0
#include "AssetRegistry/AssetRegistryModule.h"
#else
#include "AssetRegistryModule.h"
#endif

#define LOCTEXT_NAMESPACE "AssetTypeActions"

#define AVV_READ(DST, SRC, POSITION, TYPE, NUM_ELEMENTS) memcpy(&DST, &SRC[POSITION], sizeof(TYPE) * NUM_ELEMENTS); POSITION += sizeof(TYPE) * NUM_ELEMENTS;

FAVVFileActions::FAVVFileActions()
{

}

/* FAssetTypeActions_Base overrides
 *****************************************************************************/

bool FAVVFileActions::CanFilter()
{
    return true;
}

void FAVVFileActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
    auto AVVFiles = GetTypedWeakObjectPtrs<UAVVFile>(InObjects);

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 0
    FName StyleName = FAppStyle::GetAppStyleSetName();
#else
    FName StyleName = FEditorStyle::GetStyleSetName();
#endif

    Section.AddMenuEntry(
        "AVVFile_CreateskeletonSkeletalMesh",
        LOCTEXT("AVVFile_CreateSkeletalMesh", "Create Actor Attachment SkeletalMesh"),
        LOCTEXT("AVVFile_CreateSkeletalMeshTooltip", "Creates a SkeletalMesh to attach actors to."),
        FSlateIcon(StyleName, "ClassIcon.Material"),
        FUIAction(
            FExecuteAction::CreateSP(this, &FAVVFileActions::ExecuteCreateActorAttachmentSkeleton, AVVFiles),
            FCanExecuteAction()
        )
    );

    // The code block below will replace the one above once ContainsSkeleton property is serialized into the AVVFile uassets

    /*
    TArray<UObject*> FilesWithSkeletons;

    for (auto Object : InObjects)
    {
        auto Asset = Cast<UAVVFile>(Object);
        if (Asset->GetStreamableAVVData().ContainsSkeleton > 0)
        {
            FilesWithSkeletons.Add(Object);
        }
    }
    if (FilesWithSkeletons.Num() > 0)
    {
        auto AVVFiles = GetTypedWeakObjectPtrs<UAVVFile>(FilesWithSkeletons);

        Section.AddMenuEntry(
            "AVVFile_CreateskeletonSkeletalMesh",
            LOCTEXT("AVVFile_CreateSkeletalMesh", "Create SkeletalMesh"),
            LOCTEXT("AVVFile_CreateSkeletalMeshTooltip", "Creates a SkeletalMesh to attach actors to."),
            FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.Material"),
            FUIAction(
                FExecuteAction::CreateSP(this, &FAVVFileActions::ExecuteCreateSkeletalMesh, AVVFiles),
                FCanExecuteAction()
            )
        );
    }
    */
}

uint32 FAVVFileActions::GetCategories()
{
#if ENGINE_MAJOR_VERSION == 4
    return EAssetTypeCategories::MaterialsAndTextures | EAssetTypeCategories::Media;
#else
    return EAssetTypeCategories::Materials | EAssetTypeCategories::Media;
#endif
}


FText FAVVFileActions::GetName() const
{
    return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_AVVFile", "AVV");
}

UClass* FAVVFileActions::GetSupportedClass() const
{
    return UAVVFile::StaticClass();
}


FColor FAVVFileActions::GetTypeColor() const
{
    return FColor::Green;
}

bool FAVVFileActions::HasActions(const TArray<UObject*>& InObjects) const
{
    return true;
}

bool FAVVFileActions::IsImportedAsset() const
{
    return true;
}

void FAVVFileActions::GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const
{
    for (auto& Asset : TypeAssets)
    {
        const auto AVVFile = CastChecked<UAVVFile>(Asset);
        if (AVVFile)
        {
            OutSourceFilePaths.Add(AVVFile->GetPath());
        }
    }
}

void FAVVFileActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{

}

/* FAssetTypeActions_AVVFile callbacks
 *****************************************************************************/

void FAVVFileActions::ExecuteCreateActorAttachmentSkeleton(TArray<TWeakObjectPtr<UAVVFile>> Objects)
{
    IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
    const FString DefaultSuffix = TEXT("_SkeletalMesh");

    if (Objects.Num() == 1)
    {
        auto Object = Objects[0].Get();

        if (Object != nullptr)
        {
            FStreamableAVVData& streamableData = (FStreamableAVVData&)Object->GetStreamableData();
            if (streamableData.Version != AVV_VERSION)
            {
                FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("Unsupported AVV Version", "The AVV version is not supported, so a SkeletalMesh cannot be generated."));
                return;
            }

            uint8_t* data = streamableData.ReadMetaData();
            size_t readPos = 0;

            uint32_t containerType;
            uint32_t containerSize;

            uint32_t metaContainerCount;
            AVV_READ(metaContainerCount, data, readPos, uint32_t, 1);

            // Find skeleton containers.
            bool has_skeleton_data = false;
            for (uint32_t i = 0; i < metaContainerCount; ++i)
            {
                AVV_READ(containerType, data, readPos, uint32_t, 1);
                AVV_READ(containerSize, data, readPos, uint32_t, 1);

                if (containerType == AVV_META_SKELETON)
                {
                    has_skeleton_data = true;
                    break;
                }
                else
                {
                    readPos += containerSize;
                }
            }

            if (!has_skeleton_data)
            {
                FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("No Skeleton Data", "The selected AVV contains no skeleton data and a SkeletalMesh cannot be generated."));
                return;
            }

            // Determine an appropriate name
            FString Name;
            FString PackagePath;
            CreateUniqueAssetName(Object->GetOutermost()->GetName(), TEXT("_SkeletalMesh"), PackagePath, Name);

            // Create the factory used to generate the asset
            UAVVSkeletalMeshFactory* SkeletalMeshFactory = NewObject<UAVVSkeletalMeshFactory>();
            SkeletalMeshFactory->SourceAVV = Object;
            ContentBrowserSingleton.CreateNewAsset(Name, FPackageName::GetLongPackagePath(PackagePath), USkeletalMesh::StaticClass(), SkeletalMeshFactory);
        }
    }
}

#undef LOCTEXT_NAMESPACE
