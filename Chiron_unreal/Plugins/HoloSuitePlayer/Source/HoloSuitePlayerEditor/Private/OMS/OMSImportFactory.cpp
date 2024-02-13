// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "OMS/OMSImportFactory.h"
#include "OMS/OMSFile.h"
#include "Misc/Paths.h"
#include "EditorFramework/AssetImportData.h"
#include "OMS/OMSAssetImportData.h"

#define LOCTEXT_NAMESPACE "OMSImportFactory"

UOMSImportFactory::UOMSImportFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SupportedClass = UOMSFile::StaticClass();

    bCreateNew = false;
    bEditAfterNew = false;
    bEditorImport = true;
    bText = true;

    Formats.Add(TEXT("oms;Optimized Mesh Sequence"));
}

FText UOMSImportFactory::GetDisplayName() const
{
    return LOCTEXT("OMSImportFactoryDescription", "Arcturus OMS File");
}

UObject* UOMSImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
    bOutOperationCanceled = false;

    UOMSFile* NewOMSFileAsset = NewObject<UOMSFile>(InParent, InClass, InName, Flags);
    if (!NewOMSFileAsset->ImportFile(Filename))
    {
        return nullptr;
    }

    return NewOMSFileAsset;
}

UObject* UOMSImportFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    UOMSFile* NewObjectAsset = NewObject<UOMSFile>(InParent, Class, Name, Flags | RF_Transactional);
    return NewObjectAsset;
}

bool UOMSImportFactory::DoesSupportClass(UClass * Class)
{
    return (Class == UOMSFile::StaticClass());
}

bool UOMSImportFactory::FactoryCanImport(const FString& Filename)
{
    const FString Extension = FPaths::GetExtension(Filename);

    if (Extension == TEXT("oms"))
    {
        return true;
    }

    return false;
}

UClass* UOMSImportFactory::ResolveSupportedClass()
{
    return UOMSFile::StaticClass();
}

void UOMSImportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
    const auto omsFile = Cast<UOMSFile>(Obj);
    if (omsFile && ensure(NewReimportPaths.Num() == 1))
    {
        omsFile->SetPath(NewReimportPaths[0]);
    }
}

EReimportResult::Type UOMSImportFactory::Reimport(UObject* Obj)
{
    const auto OMSFile = Cast<UOMSFile>(Obj);
    if (!OMSFile)
    {
        return EReimportResult::Failed;
    }

    FString omsPath = OMSFile->GetPath();

    bool OutCanceled = false;
    if (ImportObject(Obj->GetClass(), Obj->GetOuter(), *Obj->GetName(), RF_Public | RF_Standalone, omsPath, nullptr, OutCanceled))
    {
        return EReimportResult::Succeeded;
    }

    return OutCanceled ? EReimportResult::Cancelled : EReimportResult::Failed;
}

bool UOMSImportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
    const auto OMSFile = Cast<UOMSFile>(Obj);
    if (OMSFile)
    {
        OutFilenames.Add(OMSFile->GetPath());
        return true;
    }
    return false;
}

int32 UOMSImportFactory::GetPriority() const
{
    return 0;
}

#undef LOCTEXT_NAMESPACE
