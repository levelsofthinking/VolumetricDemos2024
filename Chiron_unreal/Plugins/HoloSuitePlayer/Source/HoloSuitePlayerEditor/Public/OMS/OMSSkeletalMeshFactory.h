// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "Factories/ImportSettings.h"
#include "EditorReimportHandler.h"
#include "OMSSkeletalMeshFactory.generated.h"

/**
 *
 */
UCLASS(hidecategories = Object, collapsecategories, MinimalAPI)
class UOMSSkeletalMeshFactory : public UFactory
{
    GENERATED_UCLASS_BODY()

    //~ Begin UFactory Interface
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
    //~ Begin UFactory Interface

public:
    // OMS to create the SkeletalMesh from.
    UPROPERTY()
    class UOMSFile* SourceOMS;

    // Enable when a valid Mesh should be generated for the Skeleton so that retargeting works. Enabled by default, can be disabled if SkeletalMesh is meant only for attaching actors to the Skeleton.
    UPROPERTY()
    bool Retargeting = true;
};
