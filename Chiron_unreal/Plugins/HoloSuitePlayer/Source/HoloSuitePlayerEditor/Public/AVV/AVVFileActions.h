// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "AssetTypeActions_Base.h"

struct FToolMenuSection;
class UAVVFile;

/**
 * Implements an action for UAVVFile assets.
 */
class FAVVFileActions
    : public FAssetTypeActions_Base
{
public:

    FAVVFileActions();

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

    void ExecuteCreateActorAttachmentSkeleton(TArray<TWeakObjectPtr<UAVVFile>> Objects);
};
