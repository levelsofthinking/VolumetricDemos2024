// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "AVV/AVVImportFactory.h"
#include "AVV/AVVFile.h"
#include "Misc/Paths.h"
#include "EditorFramework/AssetImportData.h"
#include "AVV/AVVAssetImportData.h"

#define LOCTEXT_NAMESPACE "AVVImportFactory"

UAVVImportFactory::UAVVImportFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SupportedClass = UAVVFile::StaticClass();

    bCreateNew = false;
    bEditAfterNew = false;
    bEditorImport = true;
    bText = true;

    Formats.Add(TEXT("avv;Accelerated Volumetric Video"));
    Formats.Add(TEXT("amsc;Accelerated Mesh Sequence Container"));
}

FText UAVVImportFactory::GetDisplayName() const
{
    return LOCTEXT("AVVImportFactoryDescription", "Arcturus AVV File");
}

UObject* UAVVImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
    bOutOperationCanceled = false;

    UAVVFile* NewAVVObjectAsset = NewObject<UAVVFile>(InParent, InClass, InName, Flags);
    if (!NewAVVObjectAsset->ImportFile(Filename))
    {
        return nullptr;
    }

    return NewAVVObjectAsset;
}

UObject* UAVVImportFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    UAVVFile* NewObjectAsset = NewObject<UAVVFile>(InParent, Class, Name, Flags | RF_Transactional);
    return NewObjectAsset;
}

bool UAVVImportFactory::DoesSupportClass(UClass * Class)
{
    return (Class == UAVVFile::StaticClass());
}

bool UAVVImportFactory::FactoryCanImport(const FString& Filename)
{
    const FString Extension = FPaths::GetExtension(Filename);

    if (Extension == TEXT("avv") || Extension == TEXT("amsc"))
    {
        return true;
    }

    return false;
}

UClass* UAVVImportFactory::ResolveSupportedClass()
{
    return UAVVFile::StaticClass();
}

void UAVVImportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
    const auto avvFile = Cast<UAVVFile>(Obj);
    if (avvFile && ensure(NewReimportPaths.Num() == 1))
    {
        avvFile->SetPath(NewReimportPaths[0]);
    }
}

EReimportResult::Type UAVVImportFactory::Reimport(UObject* Obj)
{
    const auto avvFile = Cast<UAVVFile>(Obj);
    if (!avvFile)
    {
        return EReimportResult::Failed;
    }

    FString avvPath = avvFile->GetPath();

    bool OutCanceled = false;
    if (ImportObject(Obj->GetClass(), Obj->GetOuter(), *Obj->GetName(), RF_Public | RF_Standalone, avvPath, nullptr, OutCanceled))
    {
        return EReimportResult::Succeeded;
    }

    return OutCanceled ? EReimportResult::Cancelled : EReimportResult::Failed;
}

bool UAVVImportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
    const auto avvFile = Cast<UAVVFile>(Obj);
    if (avvFile)
    {
        OutFilenames.Add(avvFile->GetPath());
        return true;
    }
    return false;
}

int32 UAVVImportFactory::GetPriority() const
{
    return 0;
}

#undef LOCTEXT_NAMESPACE