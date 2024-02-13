// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once


#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "AssetTypeActions_Base.h"

struct FToolMenuSection;
class UOMSFile;

/**
 * Implements an action for UOMSFile assets.
 */
class FOMSFileActions
    : public FAssetTypeActions_Base
{
public:

    FOMSFileActions();

    //~ FAssetTypeActions_Base overrides

    virtual bool CanFilter() override;
    virtual void GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section) override;
    virtual uint32 GetCategories() override;
    virtual FText GetName() const override;
    virtual UClass* GetSupportedClass() const override;
    virtual FColor GetTypeColor() const override;
    virtual bool HasActions(const TArray<UObject*>& InObjects) const override;
    virtual bool IsImportedAsset() const override;
    virtual void GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const override;
    virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;

private:

    /** Callback for selecting Create Retarget Skeleton. */
    void ExecuteCreateLitMaterial(TArray<TWeakObjectPtr<UOMSFile>> Objects);
    void ExecuteCreateUnlitMaterial(TArray<TWeakObjectPtr<UOMSFile>> Objects);
    void ExecuteCreateActorAttachmentSkeleton(TArray<TWeakObjectPtr<UOMSFile>> Objects);
    void ExecuteCreateRetargetSkeleton(TArray<TWeakObjectPtr<UOMSFile>> Objects);
};
