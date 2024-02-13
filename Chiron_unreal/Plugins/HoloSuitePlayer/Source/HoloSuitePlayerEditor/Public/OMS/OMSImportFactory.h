// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "Factories/ImportSettings.h"
#include "EditorReimportHandler.h"
#include "OMSImportFactory.generated.h"

class UOMSAssetImportData;

/**
 * 
 */
UCLASS(hidecategories = Object)
class HOLOSUITEPLAYEREDITOR_API UOMSImportFactory : public UFactory, public FReimportHandler
{
    GENERATED_UCLASS_BODY()

    //~ Begin UFactory Interface
    virtual FText GetDisplayName() const override;
    //virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
    virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
    virtual bool DoesSupportClass(UClass * Class) override;
    virtual bool FactoryCanImport(const FString& Filename) override;
    virtual UClass* ResolveSupportedClass() override;

    // FReimportHandler interface
    virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;
    virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
    virtual EReimportResult::Type Reimport(UObject* Obj) override;
    virtual int32 GetPriority() const override;
    // End of FReimportHandler interface
};
