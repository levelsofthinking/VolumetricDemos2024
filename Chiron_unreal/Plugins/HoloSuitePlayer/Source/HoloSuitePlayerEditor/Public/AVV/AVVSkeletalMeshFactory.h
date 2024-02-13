// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "Factories/ImportSettings.h"
#include "EditorReimportHandler.h"
#include "AVVSkeletalMeshFactory.generated.h"

/**
 *
 */
UCLASS(hidecategories = Object, collapsecategories, MinimalAPI)
class UAVVSkeletalMeshFactory : public UFactory
{
    GENERATED_UCLASS_BODY()

    //~ Begin UFactory Interface
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
    //~ Begin UFactory Interface

public:
    // AVV to create the SkeletalMesh from.
    UPROPERTY()
    class UAVVFile* SourceAVV;

};
